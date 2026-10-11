# Cryonis

BotW's Cryonis for Tears of the Kingdom 1.2.1: raise climbable ice pillars from water.

## Controls

- Hold L, hover Amiibo in the ability wheel and click the right stick to aim. Holding L still opens
  the normal wheel.
- A builds a pillar on valid water, or shatters the highlighted pillar.
- Building closes targeting; after the recharge, tap L to reopen it. Tap L or B to exit.
- Cryonis stays selected for the game session until another ability is chosen from the wheel.
- Swimming disables targeting, and returning to land restores it. Move the camera down to aim
  beneath Link.

The translucent preview faces Link and projects a snowflake onto the water beneath it. Built ice
keeps that orientation, grows from below the surface and can be climbed. Up to three pillars stay
in the world; a fourth replaces the oldest once it has been created. Pillars stay after targeting
ends. A held A press stays consumed until it is released, and the game's interaction prompts are
hidden only while targeting is drawn. The original BotW sounds play on construction and breakage.

Pillars are 90% of the original 3x4x3 m (2.7x3.6x2.7 m), with the settled base .18 m underwater.
Placement reach is 60 m and the recharge is 50 gameplay frames. Water highlighting reaches 375 m
and fades from 300 m. Submerged ice shows through the water, pillars cast the game's shadows, and
anything up to two feet below the surface is lifted with the ice. Sizes and timings are in
`src/pure/CryonisTuning.hpp`.

## Status

In development; there is no release package yet. Tested on the Citron emulator with TotK 1.2.1
only. At startup the module checks the game code it uses and stays inactive on any other version.
The mod replaces the game's ActorInfo, GameActorInfo and UI_Static files, so other mods that change
those files conflict with it.

## Building from source

This folder contains only the mod's own code. It is **not** a complete project and comes with no
guarantee that it builds or works as-is; you set up the toolchain and framework yourself.

- Toolchain: devkitPro devkitA64. Native source uses C++26.
- Framework: [exlaunch](https://github.com/shadowninja108/exlaunch) (GPL-2.0, not included).
  Known-good base: commit `f698816d`. The mod uses no inline hooks.
- The renderer includes `nvn/nvn.h`; supply compatible NVN declarations. They are not included.
- Layout: place `src/program/main.cpp` and the `src/engine`, `src/feature`, `src/presentation` and
  `src/pure` folders in an exlaunch project, with those four folders and
  `../runtime-support/include` on the include path.
- Use exlaunch's own `source/program/{setting,loggers,version,offsets}.hpp`. In `setting.hpp` set
  `EXL_MODULE_NAME "totk-cryonis"`, keep `EXL_USE_FAKEHEAP`, remove `EXL_DEBUG`, and use
  `HeapSize 0x40000`, `JitSize 0x8000`, `InlinePoolSize 0x2000`, `LogBufferSize 1024`. Every hook
  is installed at a fixed offset, so the reloc table in `offsets.hpp` can stay empty.
- Compile definitions: `TOTK_VERSION=121`. Compile with `-ffp-contract=off`. Program ID
  `0100F2C0115B6000`, module `subsdk9`.
- The renderer and HUD include generated headers (`CryonisShaders.hpp`, `CryonisAssets.hpp`,
  `NativeFontAtlas.hpp`). The scripts in `tools/` build the shaders, the pillar actor, the sounds
  and the reference geometry from your own TotK 1.2.1 and BotW 1.5.0 dumps, using an NVN shader
  compiler you supply. `build_pillar.py` and `import_audio.py` import helper modules from the
  author's private repository, which are not included. The HUD's game-font renderer and its baked
  font atlas are also private and not included. None of the generated output is included here.

## Tests

`tests/run_host_tests.ps1` builds and runs the three host test programs with CMake and a C++20
compiler; on Windows it enters the MSVC developer environment itself. The tests need no game data.

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## License

First-party source is MIT; see `LICENSE` at the repository root. The compiled module includes
external GPL-2.0 exlaunch code. See `NOTICE.txt`.
