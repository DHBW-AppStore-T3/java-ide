# code-server with JDK 21 and Maven for the pod runtime (appstore.yaml). One container per person; the
# platform sets user (UID 1000), a read-only root filesystem and the password (env PASSWORD).

# Build stage for libpeerbroadcast.so (peerbroadcast/peerbroadcast.c): the compiler stays out of the final image. Same
# base as the final image, so the library is built against the glibc it runs with.
FROM docker.io/codercom/code-server:4.144.0 AS peerbroadcast
USER root
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends gcc libc6-dev \
 && rm -rf /var/lib/apt/lists/*
COPY peerbroadcast/peerbroadcast.c /build/peerbroadcast.c
RUN gcc -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC -shared -Wl,-z,relro,-z,now \
      -o /build/libpeerbroadcast.so /build/peerbroadcast.c -ldl -lpthread
USER 1000

FROM docker.io/codercom/code-server:4.144.0

# JDK and Maven come from the Debian packages of the base image (Debian 13), so there is no extra download source.
# fixuid (a Go binary in the upstream image) only serves upstream's own entrypoint, which the ENTRYPOINT below
# replaces; the platform sets the UID itself, so it is removed (same as in Online-IDE).
USER root
RUN rm -f /usr/local/bin/fixuid \
 && apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      openjdk-21-jdk-headless \
      maven \
 && rm -rf /var/lib/apt/lists/*

# The extensions live outside /home/coder: the storage volume is mounted there at runtime and would hide them.
# Versions are pinned (Open VSX); the stable releases, not the nightly builds.
RUN mkdir -p /opt/code-server/extensions && chown 1000:1000 /opt/code-server/extensions
USER 1000
RUN code-server --user-data-dir /tmp/build-userdata --extensions-dir /opt/code-server/extensions \
      --install-extension redhat.java@1.58.0 \
      --install-extension vscjava.vscode-java-debug@0.60.0 \
 && rm -rf /tmp/build-userdata

# Security updates the base image does not have yet, at every CI build: app-ci.yml sets SECURITY_REFRESH to a new
# value for each run, so the build cache cannot keep an older package state (app-registry README, "Sicherheitsupdates
# im Image"). After the expensive steps, which stay cached; everything below is rebuilt in each run.
USER root
ARG SECURITY_REFRESH
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get upgrade -y --no-install-recommends \
 && rm -rf /var/lib/apt/lists/*
USER 1000

# UDP "broadcast" between the environments of a deployment. The platform has no layer-2 broadcast (it would reach the
# pods of other deployments), so a send to 255.255.255.255, a multicast address or the subnet's broadcast address is
# turned into a send to every address of the Service "peers" (see peerbroadcast.c). LD_PRELOAD covers every process of
# the container: the terminal, the JVM that runs a student's program, the debugger.
COPY --from=peerbroadcast /build/libpeerbroadcast.so /opt/appstore/libpeerbroadcast.so
ENV LD_PRELOAD=/opt/appstore/libpeerbroadcast.so

# The cluster is IPv6-primary: listen on all families, not on 127.0.0.1.
# No entrypoint script: it may try sudo, which the platform forbids.
ENTRYPOINT ["dumb-init", "/usr/bin/code-server"]
CMD ["--bind-addr", "[::]:8080", "--auth", "password", "--disable-telemetry", \
     "--extensions-dir", "/opt/code-server/extensions", "/home/coder"]
