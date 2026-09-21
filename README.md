# Retro3DO Core

The 3DO Interactive Multiplayer emulation core: ARM60, MADAM, CLIO, the DSP,
SPORT, VDLP, XBUS, the PBUS pad chain, and CD/CHD media.

It is the core and nothing else. There is no user interface here, no window, no
input backend and no platform layer — those belong to whichever application is
wrapping it, and there is more than one of those:

| Application | Platform | Interface |
|---|---|---|
| [Retro-3DO](https://github.com/CrownParkComputing/Retro-3DO) | Android, Linux, Windows | C++, SDL3 and Dear ImGui |
| [Clio](https://github.com/CrownParkComputing/Clio) | iOS | Swift, SwiftUI and Metal |

This is an original codebase, not a fork of any other emulator. Its historical
reference exposure is disclosed in [docs/PROVENANCE.md](docs/PROVENANCE.md),
which must be read before anyone describes it as clean-room.

## Using it

```cmake
add_subdirectory(path/to/retro3do-core)
target_link_libraries(your_app PRIVATE retro3do_core)
```

```c
#include "retro3do.h"

retro3do_t *emu = retro3do_create();
retro3do_load_bios(emu, "panafz10.bin");
retro3do_load_disc(emu, "game.chd");
retro3do_start(emu);                       /* the core runs its own thread */

retro3do_frame frame;
if (retro3do_acquire_frame(emu, &frame)) { /* only when there is a new one */
    upload(frame.pixels, frame.width, frame.height);   /* XRGB8888 */
}
retro3do_audio_pull(emu, buffer, 512);     /* interleaved 16-bit stereo */
retro3do_pad_press(emu, 0, RETRO3DO_PAD_A, 1);

retro3do_destroy(emu);
```

`include/retro3do.h` is the whole interface, and it is plain C: Swift imports it
directly through a module map, so neither application carries a language bridge.
The header documents what may be called from which thread, and why the core owns
the emulator thread rather than each application running its own.

**Everything under `core/` is an implementation detail.** The CMake target
exports only `include/`, so an application cannot include a machine header even
by accident — the machine changes shape as hardware understanding improves, and
the interface is the thing that does not. If a front end needs something the
core will not tell it, add it here rather than widening an include path.

## Layout

| Path | What |
|---|---|
| `include/retro3do.h` | The public interface. The only header an application sees. |
| `abi/retro3do.cpp` | Its implementation: translation, and no decisions. |
| `core/` | The machine. C++20, the standard library and libchdr, nothing else. |
| `tests/` | The machine's own tests, plus `test_abi.cpp`, which includes only the public header and so fails the way an application would. |
| `tools/gamecheck` | Boots a whole library headless and reports what each title did. |
| `tools/opera_oracle` | A second implementation to diff against. A measuring instrument; nothing of it is linked in. |
| `demo/` | Source of the project-owned demonstration ROM built into the core. |
| `docs/` | Hardware notes, provenance, third-party notices. |
| `third_party/libchdr` | Vendored, built with `CHDR_WANT_RAW_DATA_SECTOR` and `CHDR_WANT_SUBCODE` forced on — see the note in `CMakeLists.txt`. |

## Building and testing

```sh
cmake -S . -B build && cmake --build build -j
./build/retro3do_core_tests
```

Tests and tools are built when this is the project being built, and skipped when
it is somebody's subdirectory: an application consuming the core wants the
library, not our test run. `RETRO3DO_CORE_TESTS` and `RETRO3DO_CORE_TOOLS`
override that either way.

`gamecheck` needs discs, which are not in this repository, so it is an
instrument rather than a test — point it at a library and it becomes one:

```sh
./build/gamecheck bios.bin /path/to/discs --expect tools/gamecheck/expectations.txt
```

The expectations file records where each title reaches today as a floor, so
losing ground is a failure rather than something someone notices by eye three
weeks later. Raise a line when a title genuinely improves; never lower one to
make a run green.

## Licence

MIT, in [LICENSE](LICENSE). Bundled components keep their own terms, collected
in [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md); the distribution and
reference-source notices in [NOTICE](NOTICE) and
[docs/PROVENANCE.md](docs/PROVENANCE.md) are part of the release record. An
application shipping this core carries those notices with it.
