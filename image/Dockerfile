# code-server with JDK 21 and Maven for the pod runtime (appstore.yaml). One container per person; the
# platform sets user (UID 1000), a read-only root filesystem and the password (env PASSWORD).
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

# The cluster is IPv6-primary: listen on all families, not on 127.0.0.1.
# No entrypoint script: it may try sudo, which the platform forbids.
ENTRYPOINT ["dumb-init", "/usr/bin/code-server"]
CMD ["--bind-addr", "[::]:8080", "--auth", "password", "--disable-telemetry", \
     "--extensions-dir", "/opt/code-server/extensions", "/home/coder"]
