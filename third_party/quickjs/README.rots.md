# QuickJS Vendor Notes

Vendored runtime: upstream QuickJS `2026-06-04`

Source: `https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz`

Source archive SHA-256:

```text
b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a
```

License: MIT-style license in `LICENSE`.

Local vendoring policy:
- This directory intentionally includes only the embeddable engine subset needed by the game server.
- Shell, compiler, REPL, examples, tests, docs, and QuickJS libc helper files are not built into the server.
- `quickjs-libc.c`, `quickjs-libc.h`, `qjs.c`, and `qjsc.c` are intentionally omitted from build wiring so scripts do not inherit filesystem, process, std/os module, worker, or shell surfaces.
- The runtime wrapper must set memory, stack, and interrupt limits before any builder-authored script is evaluated.
- Module loading is not enabled by the wrapper.

Build notes:
- QuickJS is compiled as C with `-std=gnu11`.
- `CONFIG_VERSION="2026-06-04"` is supplied by the game build.
