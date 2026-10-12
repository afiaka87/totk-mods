# BotW Abilities v0.1.2

This update fixes Switch startup and repeated Cryonis activation, reduces memory to 9.02 MiB,
and corrects recent-ability input on Ryubing. It keeps the `subsdk7` slot.

Breath of the Wild's Stasis, Remote Bomb and Cryonis in Tears of the Kingdom 1.2.1, in one add-on,
usable together. Each ability is the one from the standalone Stasis, Remote Bomb and Cryonis mods;
this add-on builds all three into one module so they can be enabled at the same time.

## Stasis

Freeze an object, strike it to build up force, and launch it when the freeze ends.

- Hold L, highlight Camera in the ability wheel and click the right stick to aim. Holding L still
  opens the normal wheel.
- A freezes the object under the reticle; B leaves aiming.
- Tap L to reopen Stasis aiming while it is the most recent ability, or to leave aiming.
  The reticle stays dim during cooldown; aiming is available, but freezing waits for recharge.
- Repeat L, Camera, right-stick click during a freeze to release early.
- While an object is frozen, B and L keep their normal uses, so Link can sprint or pick another
  ability.

Objects stay frozen for ten seconds and enemies for five; enemies keep their pose until the hold
ends. Sword, heavy-weapon and spear hits on a frozen object add force towards it from Link's side,
using BotW's per-hit amounts and cap, and an arrow grows and shifts from yellow to red with the
stored force. On release the object launches along the arrow. Enemies take hits but are not
launched. The cooldown is ten seconds after a full hold and shorter after an early release.

The selected object glows with cracks and a rim, is held by six chains while frozen, and leaves a
particle trail when launched. Targets are dynamic objects with a mass and enemies other than
bosses, out to 30 m along the camera's centre line. Link, NPCs and carried or fused gear are
skipped.

## Remote Bomb

BotW's sphere and cube Remote Bombs, in the Materials pouch, used through the game's own hold, throw
and arrow controls and set off remotely with a tap of L.

- After loading into gameplay, one sphere and one cube appear in Materials. They stay first when
  Materials or the throw and arrow shortcut lists are sorted.
- Hold a bomb from Materials, hold R and press Up for an aimed throw, or press Up while aiming a
  bow to fuse a bomb to an arrow.
- Tap L to set off the selected form's bomb, or the other form's bomb when only that one is out.
  To switch forms, hold L, hover Map, and click R3 for the sphere or L3 for the cube.
- L does nothing while Link is lifting or holding a bomb.
- A used form recharges for 3 seconds, then returns to Materials.
- Ultrahand can grab both bombs.

This is the Remote Bomb+ profile: blast damage 24 and a 3-second recharge. The blast is the Time
Bomb's explosion; Link takes the game's normal blast reaction and damage, and loose objects are
pushed by BotW's blast pressure calculation.

## Cryonis

Raise climbable ice pillars from water.

- Hold L, hover Amiibo in the ability wheel and click the right stick to aim.
- A builds a pillar on valid water, or shatters the highlighted pillar.
- Building closes targeting; after the recharge, tap L to reopen it. Tap L or B to exit.
- Cryonis stays selected for the game session until another ability is chosen from the wheel.
- Swimming disables targeting, and returning to land restores it. Move the camera down to aim
  beneath Link.

Up to three pillars stay in the world; a fourth replaces the oldest. Pillars are 90% of the
original size (2.7 x 3.6 x 2.7 m), placement reach is 60 m, and anything up to two feet below the
surface is lifted with the ice.

## Using them together

- Choosing an ability from the wheel, built-in or added, puts the other added abilities away.
- A Remote Bomb left out after choosing another ability still answers L, as in BotW. Once it has
  gone off, L returns to the chosen ability.
- Stasis's countdown ring sits in the top left beside the ability icon. The bomb icon and its
  recharge ring sit to its right, at the same size.
- Stasis and Cryonis play the original BotW sounds.

## Requirements and installation

Requires Tears of the Kingdom **1.2.1**, build `9B4E43650501A4D4`.

**Emulator:** extract `botw-abilities-v0.1.2-emulator-subsdk7.zip` and copy its `BotW Abilities`
folder into the game's mod folder (right-click the game, **Open Mod Data Location**). Enable
**BotW Abilities** in the game's add-on list.

**Switch (Atmosphere):** extract `botw-abilities-v0.1.2-switch-subsdk7.zip` and copy its
`0100F2C0115B6000` folder into `atmosphere/contents/` on the SD card.

**Upgrading from v0.1.0:** remove the old BotW Abilities add-on before installing this update.
On Switch, remove this mod's old `exefs/subsdk5` before copying the new files. Leaving both
modules installed loads the abilities twice. Preserve files belonging to other mods.

Both archives contain the same files: `exefs/subsdk7`, `exefs/main.npdm`, and 25 files under
`romfs/`. Keep both `romfs/BotwAbilities/Textures/*.bin` files; they contain the streamed
texture sources used by the shared emulator/Switch build.

## Compatibility

- BotW Abilities occupies the `subsdk7` code slot. Do not enable another mod that supplies
  `subsdk7`; two of them can stop the game from booting.
- Do not enable it together with the standalone Stasis, Remote Bomb or Cryonis mods.
- It replaces the interface sound bank (`UI_Static`), five RSDB tables (actor, attachment, game
  actor, pouch and tag), the save-data schema (`GameDataList`), the English text archive, the
  effect and sound link tables, the effect file list and the resource-size table, and adds five
  actor packs, two models and four inventory icons. Mods that replace any of those files conflict
  unless their changes are merged. If you use
  another mod that ships its own resource-size table, merge the tables with a tool such as TKMM.
- Ice, bombs and interface elements retain their native resolution; glow uses an 800x450 buffer.
  Game render resolutions above 1600x900 are outside the supported glow/depth capacity.

## Known issues

- Tested individually on Switch hardware and in Citron and Ryubing. Combinations with other
  code mods have not yet been tested on Switch.
- At startup each ability checks the game code it uses and stays inactive on any other game
  version.
- Stasis does not highlight nearby objects while aiming; only the selected target is highlighted.
- Remote Bomb does not yet match BotW's full physics and damage profiles, water behaviour,
  environmental destruction or bomb-to-bomb interaction.

## Building from source

The source for the three abilities is in the `stasis`, `remote-bomb`, `cryonis` and
`runtime-support` folders of this repository; this folder adds the combined module's entry point
and the tool that merges the abilities' game data. It is **not** a complete project and comes with
no guarantee that it builds or works as-is; you set up the toolchain and framework yourself.

- Toolchain: devkitPro devkitA64. Native source uses C++26.
- Framework: [exlaunch](https://github.com/shadowninja108/exlaunch) (GPL-2.0, not included).
  Known-good base: commit `f698816d`. The abilities use no inline hooks.
- The renderers include `nvn/nvn.h`; supply compatible NVN declarations. They are not included.
  The shared font renderer also needs its `native-font/NvnAbi.hpp` declarations and a generated
  `NativeFontShaders.hpp` from `runtime-support/shaders/nativefont.{vert,frag}`. SDK declarations,
  compiled shader arrays and owned-game font artwork are not in the public source.
- Layout: compile `src/program/main.cpp` from this folder with each ability's `src/engine`,
  `src/feature`, `src/presentation`, `src/pure` and `src/program` folders (leaving out each
  ability's own `src/program/main.cpp`), with those folders and `../runtime-support/include` on
  the include path.
- Use exlaunch's own `source/program/{setting,loggers,version,offsets}.hpp`. In `setting.hpp` set
  `EXL_MODULE_NAME "botw-abilities"`, keep `EXL_USE_FAKEHEAP`, remove `EXL_DEBUG`, and use
  `HeapSize 0x40000`, `JitSize 0x8000`, `InlinePoolSize 0x2000`, `LogBufferSize 1024`. Every hook
  is installed at a fixed offset, so the reloc table in `offsets.hpp` can stay empty.
- Compile definitions: `TOTK_VERSION=121`, `BOTW_ABILITIES_COMPACT_UI=1` and
  `BOTW_ABILITIES_STREAM_TEXTURES=1`; for the Remote Bomb sources also
  `REMOTE_BOMB_UPGRADED=1` and `REMOTE_BOMB_HUD_BESIDE_STASIS=1`. Compile with
  `-ffp-contract=off`. Program ID `0100F2C0115B6000`, module `subsdk7`.
- Build each ability's generated headers and game data as its own README describes. Run
  `tools/stream_textures.py` and `tools/compact_font.py`; put their generated header directory
  before member include directories and include both texture sidecars in RomFS. Link the
  authored shared native-font sources and generate their shaders and atlas. The test-only SD
  logger and periodic memory sampler are removed from this release.
  `tools/compose_resources.py` merges the three abilities' game data into one RomFS against your
  own TotK 1.2.1 dump, and `tools/build_release_restbl.py` builds the resource-size table for it.
  They use helper modules and a resource-table tool from the author's private repository, which
  are not included. The game data is not part of this source; it is only in the release archives.

## Tests

Each ability folder has its own host tests; see its README.

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## Credits

- exlaunch by shadowninja108 and contributors.
- devkitPro.

## License

MIT - see `LICENSE` at the repository root. `NOTICE.txt` describes the exlaunch dependency and
the game files in the release archives.
