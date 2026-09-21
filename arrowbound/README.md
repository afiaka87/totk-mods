# Arrowbound

Activate the Arrowbound Emblem in Key Items, shoot a bow, and follow the arrow with the paraglider
open. Arrows keep their vanilla speed, gravity, arc, range and lifetime. Aim in midair with normal
slow motion; releasing the shot ends slow motion for the flight.

## Controls and behavior

- Activate/deactivate the emblem to toggle arrow following. Both states use the arrow icon.
- Press B during following to let go into ordinary paragliding.
- Draw the bow again to retire the old arrow before aiming another shot.
- Hit a climbable wall to turn toward it and enter normal climbing. Other impacts finish in the glider.
- No targeting reticle, chain or arrow refund is added by this feature.

The same feature source is imported by [Glideshot](../glideshot), which also supplies manual
ZL + R3 wall targeting. Glideshot and Arrowbound must not be enabled as separate code add-ons
together: the combined Glideshot build already contains Arrowbound.

## Persistence and the emblem

`sd:/arrowbound/settings.bin` remembers activation. Emulators use their emulated SD card.
The preference is shared by standalone Arrowbound and Glideshot across all saves/profiles on
that SD card; loading an older save does not rewind it. Missing or invalid settings default off.
Storage failures retain a session-only choice. No unrelated vanilla flag is used for this setting.

The mod grants the emblem after the live inventory is ready, without requiring another item to
be selected. New game saves omit its unearned All's Well carrier; naturally earned All's Well
is preserved. Older saves remain unchanged until saved again. Quest progress is not reset.
No game assets, replacement ROMFS files or custom GameData schema are distributed.

## Requirements and validation

Requires Tears of the Kingdom 1.2.1, build `9B4E43650501A4D4`, and an exefs-compatible loader.
The feature is accepted on Eden and physical Switch inside combined Glideshot. The independent
standalone entry point also builds, but the latest code-only standalone package has not received
a separate observed hardware test. This folder is source, not a standalone binary download.

## Building from source

This contains only mod-owned source, not a complete Switch build environment. No guarantee is
made that it builds as-is; provide your own devkitPro/devkitA64 toolchain, exlaunch project and
the required game-facing headers. The native source uses C++26.

- Framework: exlaunch (GPL-2.0), known-good base `f698816d`, not included. The combined Glideshot
  host also requires its documented InlineFloatCtx fix for the manual jump hook.
- Standalone: compile `src/program/main.cpp`, `src/program/modules/arrowbound` and `src/engine`.
  Include `src/program`, `src/pure`, `src/engine`, `src/support` and `src/include`.
- Combined: `cmake/ImportArrowbound.cmake` imports the feature object target without its standalone
  entry point, using the host's exlaunch configuration and exposing `src/include/arrowbound/Module.hpp`.
- Use exlaunch's consumer templates, which are not included. Set `EXL_MODULE_NAME "arrowbound"`,
  `EXL_USE_FAKEHEAP`, no `EXL_DEBUG`, `HeapSize 0x10000`, `JitSize 0x4000`,
  `InlinePoolSize 0x1000`, `LogBufferSize 512`, and an empty reloc table.
- Compile definition `TOTK_VERSION=121`; program ID `0100F2C0115B6000`; module `subsdk9`.

## Host tests

Run `tests/run_host_tests.ps1` with CMake, Ninja and a C++23 compiler installed. It fetches doctest
2.4.11 if no vendored copy is available. The 53 cases cover following, ownership, wall capture,
carrier saving, menu text and persistence failure handling. Engine-facing tests use local stubs;
the default suite needs no game files. Glideshot's test runner also runs this suite.

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## Credits and license

Original mod source: MIT, see the repository's root LICENSE. exlaunch is GPL-2.0 and is not
included here; compiled modules include it. Tests use doctest (MIT); native builds use devkitPro.
See NOTICE.txt for source links and the dependency notice.
