# 32-bit Linux build/run environment for RotS.
#
# The game's Makefile forces a 32-bit build (-m32), which cannot run natively on
# Apple Silicon / modern macOS. This image provides an i386 Linux toolchain so the
# code compiles and runs UNCHANGED. On arm64 hosts Docker runs it via QEMU emulation.
#
# Build/run with docker compose (see docker-compose.yml) or scripts/rots-docker.sh.
FROM --platform=linux/386 i386/debian:bullseye

# g++ 10 (supports the Makefile's -std=c++1z) + make. The game Makefile links no extra
# libraries; libgtest-dev (prebuilt i386 static libs) is needed only by the unit-test
# suite under src/tests (links -lgtest -lgtest_main). telnet/procps are dev conveniences.
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make telnet procps ca-certificates libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /rots
CMD ["bash"]
