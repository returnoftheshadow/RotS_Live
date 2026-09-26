# 32-bit Linux build/run environment for RotS.
#
# The game's Makefile forces a 32-bit build (-m32), which cannot run natively on
# Apple Silicon / modern macOS. This image provides an i386 Linux toolchain so the
# code compiles and runs UNCHANGED. On arm64 hosts Docker runs it via QEMU emulation.
#
# Build/run with docker compose (see docker-compose.yml) or scripts/rots-docker.sh.
#
# Pinned by digest: a republished tag could carry a newer security libc6 than the snapshot
# below has a libc6-dev for.
FROM --platform=linux/386 i386/debian:bullseye@sha256:014be3f6cfb9ca874136360a8365b4d2849e8f6269733ee1e2cf1ad9f1b5bd00

# apt reads snapshot.debian.org at the base image's snapshot date. The base image carries a
# bullseye-security libc6, and libc6-dev must match it exactly. The live bullseye-security index
# still lists that libc6-dev, but the mirrors have pruned its pool file (404), so every source is
# frozen at the snapshot date instead, which freezes the toolchain with it.
RUN printf '%s\n' \
        'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20260623T000000Z bullseye main' \
        'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/20260623T000000Z bullseye-security main' \
        'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20260623T000000Z bullseye-updates main' \
        > /etc/apt/sources.list

# g++ 10 (supports -std=c++1z/c++17) + make. The CMake build (src/CMakeLists.txt) also
# needs cmake, GoogleTest (libgtest-dev) for the ageland_tests suite, and libcrypt-dev for
# the crypt() link; pkg-config is a CMake convenience. telnet/procps are dev conveniences.
RUN apt-get -o Acquire::Retries=3 update && apt-get -o Acquire::Retries=3 install -y --no-install-recommends \
        g++ make cmake libgtest-dev libcrypt-dev pkg-config telnet procps ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /rots
CMD ["bash"]
