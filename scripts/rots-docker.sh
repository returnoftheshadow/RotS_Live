#!/usr/bin/env bash
# Helper for building and running RotS in the 32-bit Linux container.
#
# Usage:
#   scripts/rots-docker.sh build      Build the i386 toolchain image.
#   scripts/rots-docker.sh compile    Run `make setup` + `make all` in the container.
#   scripts/rots-docker.sh test       Run the CMake build + gtest suite (`make test`).
#   scripts/rots-docker.sh boot       Compile (if needed) and start the server on :1024.
#                                     Runs WITHOUT -p, so you can connect by plain telnet.
#   scripts/rots-docker.sh shell      Drop into an interactive shell in the container.
#
# Connect to a running server from the host with:  telnet localhost 1024
#
# NOTE: booting requires world files at lib/world/ (see docs/BUILD.md). Compiling does not.
set -euo pipefail
cd "$(dirname "$0")/.."

cmd="${1:-boot}"
case "$cmd" in
  build)
    docker compose build
    ;;
  compile)
    docker compose run --rm rots bash -lc 'cd /rots/src && make setup && make all'
    ;;
  test)
    # Build the TEST target (ageland_tests, not just ageland) and run the gtest binary
    # directly, passing any extra args through, e.g.:
    #   scripts/rots-docker.sh test --gtest_filter=PlayerFinalize.*
    # (`make build` only builds `ageland`; the test binary is the `ageland_tests` target.
    #  ctest's gtest_discover_tests PRE_TEST mode finds 0 tests under bullseye's cmake 3.18,
    #  but the i386 test binary itself runs fine under QEMU, so we invoke ./bin/tests directly.)
    shift || true
    docker compose run --rm rots bash -lc \
      'cd /rots && cmake -S src -B build && cmake --build build --target ageland_tests -j16 && ./bin/tests "$@"' \
      _ "$@"
    ;;
  boot)
    if [ ! -d lib/world ]; then
      echo "ERROR: lib/world/ is missing — the server cannot boot without world files." >&2
      echo "See docs/BUILD.md (\"World files\")." >&2
      exit 1
    fi
    # -p is intentionally omitted so raw telnet connections work (no proxy IP header).
    docker compose run --rm --service-ports rots bash -lc \
      'cd /rots/src && make setup && make all && cd /rots && exec ./bin/ageland'
    ;;
  shell)
    docker compose run --rm --service-ports rots bash
    ;;
  *)
    echo "Unknown command: $cmd (use build|compile|test|boot|shell)" >&2
    exit 1
    ;;
esac
