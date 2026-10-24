# code-server with JDK 21 and Maven for the pod runtime (appstore.yaml). One container per person; the
# platform sets user (UID 1000), a read-only root filesystem and the password (env PASSWORD).
FROM docker.io/codercom/code-server:4.143.1

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

# The cluster is IPv6-primary: listen on all families, not on 127.0.0.1.
# No entrypoint script: it may try sudo, which the platform forbids.
ENTRYPOINT ["dumb-init", "/usr/bin/code-server"]
CMD ["--bind-addr", "[::]:8080", "--auth", "password", "--disable-telemetry", \
     "--extensions-dir", "/opt/code-server/extensions", "/home/coder"]
