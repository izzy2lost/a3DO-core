# Retro3DO Core

The 3DO Interactive Multiplayer emulation core behind
[Retro-3DO](https://github.com/CrownParkComputing/Retro-3DO): ARM60, MADAM,
CLIO, the DSP, SPORT, VDLP, XBUS, the PBUS pad chain, and CD/CHD media.

It is the core and nothing else. There is no user interface here, no window, no
input backend and no platform layer — those belong to whichever application is
wrapping it, and there is more than one of those. The core compiles against the
C++ standard library and libchdr, and has no other dependency.

## Using it

```cmake
add_subdirectory(path/to/retro3do-core)
target_link_libraries(your_app PRIVATE retro3do_core)
```

Headers are reached from the repository root, so the include spelling is
`#include "core/bus.h"`.

## Layout

| Path | What |
|---|---|
| `core/` | The emulator. 40 files, no dependency on anything outside itself. |
| `third_party/libchdr` | Vendored, built with `CHDR_WANT_RAW_DATA_SECTOR` and `CHDR_WANT_SUBCODE` forced on — see the note in `CMakeLists.txt`. |

## Provenance

This is an original codebase, not a fork. Its historical reference exposure is
disclosed in the parent project's `docs/PROVENANCE.md`.
