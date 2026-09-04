# 32-bit Linux build/run environment for RotS.
#
# The game's Makefile forces a 32-bit build (-m32), which cannot run natively on
# Apple Silicon / modern macOS. This image provides an i386 Linux toolchain so the
# code compiles and runs UNCHANGED. On arm64 hosts Docker runs it via QEMU emulation.
#
# Build/run with docker compose (see docker-compose.yml) or scripts/rots-docker.sh.
FROM --platform=linux/386 i386/debian:bullseye

# g++ 10 (supports -std=c++1z/c++17) + make. The CMake build (src/CMakeLists.txt) also
# needs cmake, GoogleTest (libgtest-dev) for the ageland_tests suite, and libcrypt-dev for
# the crypt() link; pkg-config is a CMake convenience. telnet/procps are dev conveniences.
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make cmake libgtest-dev libcrypt-dev pkg-config telnet procps ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /rots
CMD ["bash"]
