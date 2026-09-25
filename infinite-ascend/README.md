# Infinite Ascend v1.1.0

Extends Ascend in Tears of the Kingdom from about 20 m to 10,000 m. Native target
validation, controls, and the red/green marker stay in place. Supports 1.0.0, 1.1.0,
1.1.2, 1.2.0, 1.2.1, and 1.4.0 through 1.4.3 in Eden.

Downloads are provided on the mod sites. This repository holds source only.

## What it changes

- Reach: fixed 10,000 m; the game's four native span values are all derived from that one number.
- Validation: two local surface-shape rejections (steep/rough ceiling, surrounding-height
  disagreement) are waived. Every other check (floor, path, collision, world layer) stays native.
- Marker: native size through 40 m, then +1x per 240 m, capped at 4x.
- Nothing else: no HUD, menus, buttons, or game assets.

## Requirements

- Tears of the Kingdom **1.0.0, 1.1.0, 1.1.2, 1.2.0, 1.2.1, or 1.4.0 through 1.4.3**.
  One emulator archive serves all nine versions. If the twelve guarded
  instructions differ, all hooks remain off.
- Tested on the Eden emulator. Switch hardware is untested.

## Install

1. Open the game's mod/load directory (`load/0100F2C0115B6000/`).
2. Extract the emulator archive so it produces `infinite-ascend/exefs/{main.npdm, subsdk4}`.
3. Enable `infinite-ascend`. Do not enable another mod that supplies `subsdk4`.

## Building from source

This folder contains only the mod's own code. It is **not** a complete project and comes with no
guarantee that it builds or works as-is; you set up the toolchain and framework yourself.

- Toolchain: devkitPro devkitA64.
- Framework: [exlaunch](https://github.com/shadowninja108/exlaunch) (GPL-2.0, not included).
  Known-good base: commit `f698816d`. Apply the `InlineFloatCtx` fix from exlaunch issue #28 / PR #31;
  without it the marker-size hook writes the wrong float register.
- Layout: place `src/program/main.cpp`, `src/program/modules/`, and `src/pure/` in an exlaunch
  project with `src/program` and `src/pure` on the include path.
- Use exlaunch's own `source/program/{setting,loggers,version,offsets}.hpp`. In `setting.hpp` set
  `EXL_MODULE_NAME "zonai-ascend"`, keep `EXL_USE_FAKEHEAP`, remove `EXL_DEBUG`, and use
  `HeapSize 0x10000`, `JitSize 0x4000`, `InlinePoolSize 0x1000`, `LogBufferSize 512`. Leave the
  reloc table in `offsets.hpp` empty.
- Compile definition: `TOTK_VERSION=121`. Program ID `0100F2C0115B6000`, module `subsdk4`.

## Tests

- `tests/run_host_tests.ps1` - builds and runs the doctest suite for reach policy and game profiles
  (needs CMake, a C++23 compiler, and doctest; a first standalone run may fetch doctest).
- `tests/verify_main121.ps1 -Binary <main>` - checks the twelve hooked instruction words against
  your own 1.2.1 executable dump.
- `tests/verify_runtime_imports.ps1 -Elf <subsdk4.elf>` - checks the linked module for a forbidden
  runtime import.

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## Credits

- exlaunch by shadowninja108 and contributors.
- doctest (MIT); devkitPro.

## License

MIT - see `LICENSE` at the repository root. `NOTICE` describes the exlaunch dependency.

## Executable slot

Uses `subsdk4`. When upgrading, remove this mod's old `exefs/subsdk9`
from its own add-on folder before installing the new package. On Switch, remove
only the old executable belonging to this mod; preserve another mod's slot 9 file.
Older downloads used slot 9; v1.1.0 uses slot 4.
