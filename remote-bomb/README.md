# Remote Bomb

BotW's sphere and cube Remote Bombs for Tears of the Kingdom 1.2.1. Both bombs live in the
Materials pouch, are used through the game's own hold, throw and arrow controls, and are set off
remotely with a tap of L.

## Standalone controls

- After loading into gameplay, one sphere and one cube appear in Materials. They stay first when
  Materials or the throw and arrow shortcut lists are sorted.
- Hold a bomb from Materials, hold R and press Up for an aimed throw, or press Up while aiming a
  bow to fuse a bomb to an arrow. B and Link's other controls stay as they are.
- Tap L to set off the bomb last taken out of Materials, or the other bomb when only that one is
  out.
- L does nothing while Link is lifting or holding a bomb.
- A used form recharges (3 seconds, or 6 with the base profile), then returns to Materials.
- After choosing an ability from the wheel, L keeps setting off a bomb that is out; the next press
  with no bomb out starts the chosen ability.
- Ultrahand can grab both bombs, with the game's orange outline in reach and green when aimed at
  or held.

The blast is the Time Bomb's explosion. Link takes the game's normal blast reaction and damage,
and loose objects are pushed by BotW's blast pressure calculation.

## Status and released package

This folder contains the standalone entry point and shared ability source. The released
download is [BotW Abilities v0.2.3](../botw-abilities/README.md), which builds this ability
into its second wheel page (hold L, then tap R). There is no separate standalone release
package. The standalone controls above describe that development entry point.

The released bundle supports wheel selection, pickup, holding and throwing. Both activation
sounds play on Citron and Switch, neither bomb detonates on its own, and L still detonates
them. Daruk audio belongs to the bundle and is also confirmed on Switch.

The recent source cleanup and memory changes are included in that accepted bundle. The
standalone package has not received a new gameplay test or release capture. Other mods
that replace the same sound bank or actor tables require the usual data merges.

Exact original-game parity for every physics, damage, water and environmental interaction
remains unproven.

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
  `EXL_MODULE_NAME "totk-remote-bomb"`, keep `EXL_USE_FAKEHEAP`, remove `EXL_DEBUG`, and use
  `HeapSize 0x40000`, `JitSize 0x8000`, `InlinePoolSize 0x2000`, `LogBufferSize 1024`. Every hook
  is installed at a fixed offset, so the reloc table in `offsets.hpp` can stay empty.
- Compile definitions: `TOTK_VERSION=121`, and `REMOTE_BOMB_UPGRADED=1` for the Remote Bomb+
  profile (damage 24, 3-second recharge) or `0` for the base profile (damage 12, 6 seconds).
  Compile with `-ffp-contract=off`. Program ID `0100F2C0115B6000`, module `subsdk9`.
- The renderer includes generated headers (`RemoteBombShaders.hpp`, `RemoteBombAssets.hpp`,
  `RemoteBombHudAssets.hpp`, `RemoteBombEffectAssets.hpp`). The scripts in `tools/` build them,
  along with the actor, inventory, effect and sound data, from your own TotK 1.2.1 and BotW 1.5.0
  dumps. They import helper modules from the author's private repository, which are not included.
  The bomb models and inventory icons are built with Switch Toolbox through private helper scripts,
  also not included. None of the generated output is included here.

## Tests

`tests/run_host_tests.ps1` builds and runs the eleven host test programs with CMake and a C++20
compiler; on Windows it enters the MSVC developer environment itself. The tests need no game data.

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## License

First-party source is MIT; see `LICENSE` at the repository root. The compiled module includes
external GPL-2.0 exlaunch code. See `NOTICE.txt`.
