#!/usr/bin/env bash
# Smoke test: code-server starts and serves its login page, and the Java toolchain works in the platform's security
# context: java, javac and mvn run, the Java extensions are installed, and a program compiles and runs. Run by the
# central pipeline (DHBW-AppStore-T3/app-registry) against the locally built image, before the Trivy scan and before
# any push. IMAGE and PLATFORM_RUN_FLAGS (--user 1000:1000 --read-only --cap-drop ALL --security-opt
# no-new-privileges --network host) come from the pipeline; the pipeline removes the container afterwards.
set -euo pipefail

# shellcheck disable=SC2086  # PLATFORM_RUN_FLAGS is meant to split into words
docker run -d --name ide $PLATFORM_RUN_FLAGS \
  --tmpfs /tmp --tmpfs /home/coder:uid=1000,gid=1000 \
  -e PASSWORD=ci-smoke-test \
  "$IMAGE"

ok=0
for _ in $(seq 1 30); do
  if curl -fsS -o /dev/null "http://[::1]:8080/healthz"; then ok=1; break; fi
  sleep 2
done
docker logs ide
[ "$ok" = 1 ]

# Capture the body first: with pipefail, `curl | grep -q` can fail with SIGPIPE once grep has matched.
login=$(curl -fsS "http://[::1]:8080/login")
grep -qi "password" <<<"$login"

# The toolchain a student uses in the terminal.
docker exec ide java -version
docker exec ide mvn -version
extensions=$(docker exec ide code-server --user-data-dir /tmp/ud --extensions-dir /opt/code-server/extensions --list-extensions)
grep -qx "redhat.java" <<<"$extensions"
grep -qx "vscjava.vscode-java-debug" <<<"$extensions"

# A program compiles and runs without any installation.
docker exec -i ide sh -c 'cat > /tmp/Hello.java' <<'EOF'
public class Hello {
    public static void main(String[] args) {
        System.out.println("hello-java");
    }
}
EOF
out=$(docker exec ide sh -c 'cd /tmp && javac Hello.java && java Hello')
grep -qx "hello-java" <<<"$out"

# The preload library is active in every process of the container (terminal, JVM, debugger).
[ "$(docker exec ide printenv LD_PRELOAD)" = "/opt/appstore/libpeerbroadcast.so" ]
docker exec ide test -r /opt/appstore/libpeerbroadcast.so

# UDP broadcast between the environments of a deployment (image/peerbroadcast): the platform gives a deployment the
# name "peers" for all its pods but no layer-2 broadcast, so a send to 255.255.255.255 or a multicast address must reach
# every address of "peers" exactly once, and a unicast send only its target. "peers" is made to resolve to two receiver
# addresses here. Run once with an IPv4 socket and once with a dual-stack socket (the JVM default). Same security
# context as the platform, on the default network: --network host cannot take --add-host.
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
for jvm_flags in "-Djava.net.preferIPv4Stack=true" "-Djava.net.preferIPv4Stack=false"; do
  # shellcheck disable=SC2086  # jvm_flags is a single JVM option
  out=$(docker run --rm --user 1000:1000 --read-only --cap-drop ALL --security-opt no-new-privileges --tmpfs /tmp \
    --add-host peers:127.0.0.2 --add-host peers:127.0.0.3 \
    -v "$here/BroadcastCheck.java:/check/BroadcastCheck.java:ro" \
    --entrypoint java "$IMAGE" $jvm_flags /check/BroadcastCheck.java 127.0.0.2 127.0.0.3)
  echo "$out"
  grep -qx "broadcast-ok" <<<"$out"
done
