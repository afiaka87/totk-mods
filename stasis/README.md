# Stasis

BotW's Stasis for Tears of the Kingdom 1.2.1: freeze an object, strike it to build up force, and
launch it when the freeze ends.

## Controls

- Hold L, highlight Camera in the ability wheel and click the right stick to aim. Holding L still
  opens the normal wheel.
- A freezes the object under the reticle; B leaves aiming.
- Repeat L, Camera, right-stick click during a freeze to release early.
- While an object is frozen, B and L keep their normal uses, so Link can sprint or pick another
  ability.

While aiming, the camera eases above and to Link's right so he sits below and left of the centred
reticle. Objects stay frozen for ten seconds and enemies for five; enemies keep their pose and stop
thinking and animating until the hold ends. Sword, heavy-weapon and spear hits on a frozen object
add force towards it from Link's side, using BotW's per-hit amounts and cap, and an arrow grows and
shifts from yellow to red with the stored force. On release the object launches along the arrow.
Enemies take hits but are not launched. The cooldown is ten seconds after a full hold and shorter
after an early release.

The selected object glows with cracks and a rim, is held by six chains while frozen, and leaves a
particle trail when launched. A countdown and cooldown sit by the ability icon, and the original
BotW sounds play for freezing, hits, the last three seconds and release. Targets are dynamic
objects with a mass and enemies other than bosses, found along the camera's centre line out to
30 m. Link, NPCs and carried or fused gear are skipped.

## Status

In development; there is no release package yet. Tested on the Citron emulator with TotK 1.2.1
only. At startup the module checks the game code it uses and stays inactive on any other version.
Scanning nearby objects while aiming is not implemented: only the selected target is highlighted.
The effects need an internal resolution of 1920x1080 or lower. The mod replaces the game's
`UI_Static` sound bank, so other mods that change that file conflict with it.

## Building from source

This folder contains only the mod's own code. It is **not** a complete project and comes with no
guarantee that it builds or works as-is; you set up the toolchain and framework yourself.

- Toolchain: devkitPro devkitA64. Native source uses C++26.
- Framework: [exlaunch](https://github.com/shadowninja108/exlaunch) (GPL-2.0, not included).
  Known-good base: commit `f698816d`. The mod uses no inline hooks.
- The renderer includes `nvn/nvn.h`; supply compatible NVN declarations. They are not included.
- Layout: place the `src/program`, `src/engine`, `src/feature`, `src/presentation` and `src/pure`
  folders in an exlaunch project, with those five folders and `../runtime-support/include` on the
  include path.
- Use exlaunch's own `source/program/{setting,loggers,version,offsets}.hpp`. In `setting.hpp` set
  `EXL_MODULE_NAME "totk-stasis"`, keep `EXL_USE_FAKEHEAP`, remove `EXL_DEBUG`, and use
  `HeapSize 0x40000`, `JitSize 0x8000`, `InlinePoolSize 0x2000`, `LogBufferSize 1024`. Every hook
  is installed at a fixed offset, so the reloc table in `offsets.hpp` can stay empty.
- Compile definitions: `TOTK_VERSION=121`. Compile with `-ffp-contract=off`, which the charge
  arithmetic relies on. Program ID `0100F2C0115B6000`, module `subsdk9`.
- The renderer includes the generated `StasisShaders.hpp`. `tools/compile_shaders.py` builds it from
  `shaders/` with an NVN shader compiler (`uam`) and `nvdisasm` that you supply; it checks both
  tools against pinned hashes and rejects instructions the emulator cannot run.
- `tools/import_audio.py` builds the replacement `UI_Static.bars.zs` from your own BotW 1.5.0 and
  TotK 1.2.1 dumps. It imports helper modules from the author's private repository, which are not
  included. None of the generated output is included here.

## Tests

`tests/run_host_tests.ps1` builds and runs the host tests with CMake and a C++23 compiler, then the
shader compiler checks with `uv`. On Windows it enters the MSVC developer environment itself. The
tests need no game data. `tools/preview_shader.py` renders the shaders offline with ModernGL.

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## License

First-party source is MIT; see `LICENSE` at the repository root. The compiled module includes
external GPL-2.0 exlaunch code. See `NOTICE.txt`.
