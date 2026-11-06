# Build/run environment for RotS: amd64 Ubuntu 22.04, the game server's Linux and compiler
# (g++ 11.4), with the multilib toolchain that builds the game 32-bit (-m32), as the server
# does. On arm64 hosts Docker emulates the image.
#
# Build/run with docker compose (see docker-compose.yml) or scripts/rots-docker.sh.
#
# The base image and the apt snapshot below are pinned as a pair: libc6-dev and
# libc6-dev-i386 must match the image's libc6 exactly, so a snapshot newer than the image
# makes apt replace the image's libc6. The snapshot also freezes the toolchain versions.
# To move to a newer image, update the digest and set the snapshot timestamp to that
# image's org.opencontainers.image.created date.
#
# CI loads a stored copy of this image keyed on this file's SHA-256 (.github/actions/dev-image),
# so any edit here, a comment included, makes CI rebuild it from the snapshot until the edit is
# merged and its copy published.
FROM --platform=linux/amd64 ubuntu:22.04@sha256:5ec03bb3441e8b0bf3b4f9cd4629a1ae763010dc3035bb8da3ae6cf026486401

RUN printf '%s\n' \
        'deb https://snapshot.ubuntu.com/ubuntu/20260924T000000Z jammy main universe' \
        'deb https://snapshot.ubuntu.com/ubuntu/20260924T000000Z jammy-updates main universe' \
        'deb https://snapshot.ubuntu.com/ubuntu/20260924T000000Z jammy-security main universe' \
        > /etc/apt/sources.list

# The snapshot service is https-only and the base image has no CA certificates, so
# ca-certificates itself is fetched without TLS peer verification. That does not weaken
# integrity: apt checks each InRelease signature against the image's ubuntu-keyring and
# each package against the signed hashes. Everything after it is fetched with verification.
#
# The 32-bit build needs gcc-multilib, g++-multilib, libc6-dev-i386 and libcrypt-dev:i386
# (crypt() is linked 32-bit). cmake drives the CMake build (src/CMakeLists.txt) and
# pkg-config is a CMake convenience. python3 and python3-venv run the integration harness.
# telnet and procps are dev conveniences. The retries cover transient snapshot fetch failures.
RUN apt-get -o Acquire::Retries=3 -o Acquire::https::Verify-Peer=false update \
    && apt-get -o Acquire::Retries=3 -o Acquire::https::Verify-Peer=false install -y --no-install-recommends \
        ca-certificates \
    && dpkg --add-architecture i386 \
    && apt-get -o Acquire::Retries=3 update \
    && apt-get -o Acquire::Retries=3 install -y --no-install-recommends \
        g++ gcc-multilib g++-multilib libc6-dev-i386 libcrypt-dev libcrypt-dev:i386 \
        make cmake pkg-config python3 python3-venv telnet procps ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /rots
CMD ["bash"]
