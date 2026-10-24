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
