# BotW Abilities v0.2.11

Upgrading: replace the previous add-on folder so its obsolete shared ELink file is
removed. On Switch, remove the old ELink override only if it belongs to this mod;
keep any ELink file supplied by another mod or mod manager.

## Changes in v0.2.11

Fixes missing Champion and bomb effects when another mod replaces the effect-bank
index. Existing entries keep priority; BotW Abilities supplies only its four missing
private aliases. Its effects do not require a user-run merge tool.

Unarmed hits now charge Stasis using the one-handed weapon rate, including punches
from Empty-Handed Combat. The missing-weapon crash guard remains.

The combined Switch test passed with Empty-Handed Combat, ELink2 Compatibility Patch,
Master Deity Sword, Warrior of Sunlight and Silent Sorrow. Punch charging, Fury's dome,
both bomb explosions and prompt Mipha presentation were confirmed. The previous Mipha
delay's exact cause is unestablished; no recharge or presentation timing was changed.
Ryujinx has not had a direct retest. Other shared-data conflicts remain possible.

Temporary SD logging is removed from the release. Community test mods are not bundled.

## Changes in v0.2.10

Custom effects now load from a private ELink file, so other mods can replace the
shared ELink table without removing BotW Abilities effects. No manual effect merge
is required. Tested with three individual ELink overlays and a community merged
bundle in Citron, then the merged bundle on Switch. Other shared-file conflicts
still require compatible mod data; this is not a blanket TKMM compatibility claim.

Also guards Stasis against a missing weapon when processing unarmed hits.
That correction passes host tests but has not had a dedicated gameplay test.
Visuals, controls, recharge times and memory reserves are unchanged.

## Changes in v0.2.9

Fixes abilities disappearing when used alongside Zonai Survey and Self Recall.
The startup check now recognizes their combined controller hooks instead of disabling
Stasis, Cryonis, bombs, the ability wheel and Champions. Clay reports that everything
appears to work again in Citron. No new Switch test is claimed for this correction.

Existing visuals, controls, recharge times and memory reserves are unchanged.

## Changes in v0.2.8

Stasis and Cryonis now support gyro aiming, following the game's motion-control
and horizontal/vertical camera inversion settings. The pause menu receives A/B
normally while either ability is targeting. Daruk's Protection follows ZL without
requiring an equipped or raised shield, including while holding a two-handed weapon.

Gyro was accepted in Citron. The menu/Daruk candidate was cleared for release after
deployment; no separate Switch test of these changes is claimed. Earlier Champion
appearance tests on both platforms still apply. Release cleanup removes temporary
gyro sample logging; normal recharge times and all game assets remain unchanged.

## Changes in v0.2.7

Mipha, Revali, Daruk and Urbosa now appear with their original animations when
their blessings activate, then fade away. Daruk appears when his protection blocks
a hit. The existing effects, voices and recharge announcements remain.

All four appearances were accepted in Citron and on Switch. The release removes
temporary memory profiling and progress logs; lifecycle behavior and all game assets
are preserved by offline comparison. The module still maps 3.965 MiB, with the shared
8 MiB loader reservation unchanged. Native models, textures and animation allocations
are additional; the module figure is not total memory use.

## Changes in v0.2.6

Multishot bows now detonate every fused remote bomb in the volley. Three-shot
sphere and cube volleys were tested in Citron: all three bombs explode, deal
damage and recharge correctly. The two-handed Fury fix below is included.
Neither fix has had a separate Switch test. Prior Switch gameplay and audio
coverage is retained. Audio, the 3.965 MiB mapped module footprint and shared
8 MiB loader reservation are unchanged. No refactor was performed.

## Changes in v0.2.5

Urbosa's Fury now works with two-handed weapons. The growing hemisphere starts after
half a stamina wheel has been spent during the attack, regardless of starting stamina.
Holding the attack does not strike enemies; releasing it triggers Fury once.
This behavior, including starting with depleted stamina, was tested in Citron.
The two-handed fix has not had a separate Switch test. One-handed weapons and spears
retain their existing charged-release behavior. Audio assets and the 3.965 MiB mapped
module footprint are unchanged; the shared loader reservation remains 8 MiB.

## Changes in v0.2.4

Uses the shared 8 MiB loader reservation so installing this package does not overwrite the combined-mod memory fix.

The six-mod combination of Infinite Ascend, Fleet Sync, Self Recall (Switch), Zonai Hookshot with Arrowbound, BotW Abilities and Zonai Survey passed a physical Switch 1.2.1 session. Other game versions and combinations have not had a new test of the 8 MiB setting.

This update fixes Stasis selection and holds, adds sphere and cube bombs to the second
wheel page, and restores Daruk's parry feedback and the bombs' activation sounds.
Nearby Stasis targets have amber previews; the hovered target has a brighter full-model
highlight. Link's equipment stays unlit. Frozen enemies and objects no longer fall or
rotate, and breakable objects stay intact while frozen. Projectile hits use their incoming
direction. Fury retries temporarily protected targets and reaches Evermeans.

The gameplay and restored audio have been tested in Citron and on Switch. All four Champions'
abilities work on Switch, including Daruk's grunt. Both bomb shapes play their activation
sounds, stay intact without automatic detonation, and still detonate with L. Animated
Champion spirits remain absent. The module maps 4,157,440 bytes (3.965 MiB),
excluding game-managed resources and GPU memory.

Breath of the Wild's Stasis, Remote Bomb, Cryonis and Champions' Blessings in Tears of the Kingdom
1.2.1, in one add-on, usable together. Stasis, Remote Bomb and Cryonis are the abilities from the
standalone mods, built into one module so they can be enabled at the same time; the Champions'
Blessings exist only in this add-on.

## Stasis

Freeze an object, strike it to build up force, and launch it when the freeze ends.

- Hold L to open the ability wheel and tap R to turn to its second page. Highlight Stasis+ at the
  bottom and release L to aim. Tap R again to return to the normal page.
- A freezes the object under the reticle; B leaves aiming.
- Tap L to reopen Stasis aiming while it is the most recent ability, or to leave aiming.
  The reticle stays dim during cooldown; aiming is available, but freezing waits for recharge.
- Pick Stasis+ from the second page again during a freeze to release early.
- While an object is frozen, B and L keep their normal uses, so Link can sprint or pick another
  ability.

Objects stay frozen for ten seconds and enemies for five; enemies keep their pose until the hold
ends. Sword, heavy-weapon and spear hits on a frozen object add force towards it from Link's side,
using BotW's per-hit amounts and cap, and an arrow grows and shifts from yellow to red with the
stored force. On release the object launches along the arrow. Enemies take hits but are not
launched. The cooldown is ten seconds after a full hold and shorter after an early release.

Nearby eligible targets have amber previews and the hovered target has a brighter highlight.
The selected object glows with cracks and a rim, is held by six chains while frozen, and leaves
a particle trail when launched. Targets are dynamic objects with a mass and supported enemies
out to 30 m along the camera's centre line. Link, NPCs and carried or fused gear are skipped.
The world keeps moving while aiming, as in BotW.

## Remote Bomb

BotW's sphere and cube Remote Bombs, available from the second ability-wheel page and the
Materials pouch, set off remotely with a tap of L.

- Hold L, tap R and choose the sphere or cube on the second wheel page. The bomb appears
  in front of Link and uses the normal pickup action.
- After loading into gameplay, one sphere and one cube appear in Materials. They stay first when
  Materials or the throw and arrow shortcut lists are sorted.
- Hold a bomb from Materials, hold R and press Up for an aimed throw, or press Up while aiming a
  bow to fuse a bomb to an arrow.
- Tap L to set off the bomb last taken out of Materials, or the other bomb when only that one is
  out.
- L does nothing while Link is lifting or holding a bomb.
- A used form recharges for 3 seconds, then returns to Materials.
- Ultrahand can grab both bombs.
- Bombs play their native activation sequence but do not detonate on a timer.

This is the Remote Bomb+ profile with a 3-second recharge. Both shapes use native actors for
materials, glow, trails, physics and explosions. The sphere is resized to the previous bomb's
size; the cube keeps its original shape and box collider. Native explosion behavior replaces
the previous shader-rendered bomb and custom blast-pressure route.

## Cryonis

Raise climbable ice pillars from water.

- Hold L, tap R to turn the ability wheel to its second page, highlight Cryonis at the bottom
  left and release L to aim.
- A builds a pillar on valid water, or shatters the highlighted pillar.
- Building closes targeting; after the recharge, tap L to reopen it. Tap L or B to exit.
- Cryonis stays selected for the game session until another ability is chosen from the wheel.
- Swimming disables targeting, and returning to land restores it. Move the camera down to aim
  beneath Link.

Up to three pillars stay in the world; a fourth replaces the oldest. Pillars are 90% of the
original size (2.7 x 3.6 x 2.7 m), placement reach is 60 m, and anything up to two feet below the
surface is lifted with the ice.

## Champions' Blessings

BotW's four Champion abilities. Each is triggered by something Link already does, so none of them
uses the ability wheel or a new button. All four are available from the start.

- **Mipha's Grace:** when Link's hearts run out, every heart container refills and 5 temporary
  yellow hearts are added. 1 charge, 24-minute recharge.
- **Revali's Gale:** hold the jump button (X normally, B with Swap Jump Controls) through landing.
  Link stops, crouches and gathers the updraft; a chime marks it ready. Release to rise; the paraglider opens by
  itself. Releasing before the chime cancels without using a charge. Gale does not start from a
  run: land and come to a stop first. 3 charges, 6-minute recharge.
- **Daruk's Protection:** hold ZL (no shield required) and an orange shell surrounds Link. Each hit it
  blocks uses one charge, without damage, knockback or shield wear. Release and press ZL again
  after a block to bring the shell back. 6 charges, 18-minute recharge.
- **Urbosa's Fury:** release a charged spin attack (hold and release Y) and lightning strikes every
  eligible enemy within a 50 m sphere. With a two-handed weapon, spend half a stamina
  wheel during the spin to start the growing hemisphere, then release Y to strike.
  3 charges, 12-minute recharge.

Recharge starts after the last charge is used and restores every charge at once, with a chime
and the Champion's spoken ready announcement.
Holding the matching Sage's vow (Tulin for Revali, Sidon for Mipha, Yunobo for Daruk, Riju for
Urbosa) makes that blessing recharge three times faster, and inside Hyrule Castle each recharge is
three times faster again, as in BotW. Recharge pauses in menus. The blessings are off inside
shrines, where a fatal hit uses a fairy as usual.

Each blessing has its own Key Item, added when a save loads. Use it from the Key Items pouch to
switch that blessing off or back on. Charges, recharge and these switches are saved with each
save file. Enabled blessings keep their icon and charge pips in the top-left row beside Stasis
and the bombs; a ring shows recharge progress. The blessings play BotW's own sounds and voice
cues, and Gale uses BotW's crouch pose.

## Using them together

- Choosing an ability from the wheel, built-in or added, puts the other added abilities away.
- Closing the wheel on its second page keeps the equipped built-in ability.
- A Remote Bomb left out after choosing another ability still answers L, as in BotW. Once it has
  gone off, L returns to the chosen ability.
- Stasis's countdown ring sits in the top left beside the ability icon. The bomb icon, its
  recharge ring and Champion icons follow to its right, at the same size.
- Stasis and Cryonis play the original BotW sounds.

## Requirements and installation

Requires Tears of the Kingdom **1.2.1**, build `9B4E43650501A4D4`.

**Emulator:** extract `botw-abilities-v0.2.11-emulator-subsdk7.zip` and copy its `BotW Abilities`
folder into the game's mod folder (right-click the game, **Open Mod Data Location**). Enable
**BotW Abilities** in the game's add-on list.

**Switch (Atmosphere):** extract `botw-abilities-v0.2.11-switch-subsdk7.zip` and copy its
`0100F2C0115B6000` folder into `atmosphere/contents/` on the SD card.

**Upgrading from v0.1.0:** remove the old BotW Abilities add-on before installing this update.
On Switch, remove this mod's old `exefs/subsdk5` before copying the new files. Leaving both
modules installed loads the abilities twice. Preserve files belonging to other mods.

**Upgrading from v0.1.4:** replace the whole add-on folder so no file from the earlier version
remains. Existing saves load, and the Champion Key Items are added on the first load.

Both archives contain the same game payload: `exefs/subsdk7`, `exefs/main.npdm`, and resources under
`romfs/`. Keep both `romfs/BotwAbilities/Textures/*.bin` files and
`romfs/BotwAbilities/Champions/GalePose.bin`; they hold streamed textures and Gale's pose.

## Compatibility

- BotW Abilities occupies the `subsdk7` code slot. Do not enable another mod that supplies
  `subsdk7`; two of them can stop the game from booting.
- Do not enable it together with the standalone Stasis, Remote Bomb or Cryonis mods.
- It replaces the interface sound bank (`UI_Static`), five RSDB tables (actor, attachment, game
  actor, pouch and tag), the save-data schema (`GameDataList`), the English text archive, the
  sound link table, the effect file list and the resource-size table, and adds
  the pillar and bomb actor packs, the Champion effect, updraft and lightning actor packs,
  native models, textures, an effect bank, Gale's pose file and inventory, Key Item and wheel
  icons. Mods that replace any of those files conflict
  unless their changes are merged. Installing this add-on separately beside TKMM's output does
  not merge those shared files; changing load order cannot retain both mods' additions.
- TKMM compatibility is not yet verified. TKMM merges several of these formats, but its current
  merger uses priority for sound link tables and the interface sound bank, and recalculates
  resource budgets instead of preserving the supplied resource-size table. A combined setup needs
  those files and budgets checked after merging. Simply importing the mod is not a confirmed fix.
  The TKMM **Use romfslite** option selects the Optimizer's alternate file loader; it does not
  resolve shared-file conflicts.
- Bombs and Stasis use native rendering/bloom; the former shared 800x450 private glow/depth
  targets are removed. Ice and interface elements retain their existing resolution.

## Known issues and testing limits

- The two-handed Fury fix was tested in Citron; it has not had a separate Switch test.
- Multishot bomb detonation was tested with three-shot volleys in Citron. Five-shot
  bows and the new multishot fix on Switch have not been separately tested.

- Hinox and Blupee freezing and exposed Octorok highlighting are untested but considered
  likely to work based on similar beasts, fauna, swimming fish and exposed Lizalfos.
  Constructs remain unsupported by Stasis. Gyro aiming follows the game's motion-control and camera-inversion options.
- Daruk blocks attacks but does not deflect them.
- Mipha's fairy order and shrine behavior, and Castle/Sage-vow recharge multipliers, have
  not been separately verified in game.
- Bomb water behavior, environmental destruction and bomb-to-bomb interactions do not yet
  have full BotW-parity coverage. The six-mod Switch 1.2.1 combination above passed; other combinations remain unverified.
- Each ability checks the game code it uses and stays inactive on unsupported versions.

## Building from source

The source for the three abilities is in the `stasis`, `remote-bomb`, `cryonis` and
`runtime-support` folders of this repository; this folder adds the combined module's entry point,
the second wheel page, the Champions' Blessings and the tools that build the combined game data. It is **not** a complete project and comes with
no guarantee that it builds or works as-is; you set up the toolchain and framework yourself.

- Toolchain: devkitPro devkitA64. Native source uses C++26.
- Framework: [exlaunch](https://github.com/shadowninja108/exlaunch) (GPL-2.0, not included).
  Known-good base: commit `f698816d`. The abilities use no inline hooks.
- The renderers include `nvn/nvn.h`; supply compatible NVN declarations. They are not included.
  The shared font renderer also needs its `native-font/NvnAbi.hpp` declarations and a generated
  `NativeFontShaders.hpp` from `runtime-support/shaders/nativefont.{vert,frag}`. SDK declarations,
  compiled shader arrays and owned-game font artwork are not in the public source.
- Layout: compile `src/program/main.cpp`, `src/wheel/WheelPage.cpp` and `src/champions/*.cpp`
  from this folder with each
  ability's `src/engine`, `src/feature`, `src/presentation`, `src/pure` and `src/program` folders
  (leaving out each ability's own `src/program/main.cpp`), with those folders, this folder's `src`
  and `../runtime-support/include` on the include path.
- Use exlaunch's own `source/program/{setting,loggers,version,offsets}.hpp`. In `setting.hpp` set
  `EXL_MODULE_NAME "botw-abilities"`, keep `EXL_USE_FAKEHEAP`, remove `EXL_DEBUG`, and use
  `HeapSize 0x40000`, `JitSize 0x8000`, `InlinePoolSize 0x2000`, `LogBufferSize 1024`. Every hook
  is installed at a fixed offset, so the reloc table in `offsets.hpp` can stay empty.
- Compile definitions: `TOTK_VERSION=121`, `BOTW_ABILITIES_COMPACT_UI=1` and
  `BOTW_ABILITIES_STREAM_TEXTURES=1`; enable `BOTW_ABILITIES_NATIVE_SPHERE=1` and
  `BOTW_ABILITIES_NATIVE_CUBE=1` on the suite and Remote Bomb sources, plus
  `STASIS_NATIVE_BLOOM=1` on Stasis; for the Remote Bomb sources also
  `REMOTE_BOMB_UPGRADED=1`, `REMOTE_BOMB_HUD_BESIDE_STASIS=1` and `BOTW_ABILITIES_CHAMPIONS=1`; `STASIS_WHEEL_CHORD=0` for the
  Stasis sources and `CRYONIS_WHEEL_CHORD=0` for the Cryonis sources. Compile with
  `-ffp-contract=off`. Program ID `0100F2C0115B6000`, module `subsdk7`.
- Build each ability's generated headers and game data as its own README describes. Run
  `tools/stream_textures.py` and `tools/compact_font.py`; put their generated header directory
  before member include directories and include both texture sidecars in RomFS. Link the
  authored shared native-font sources and generate their shaders and atlas. Native actor/model preparation also uses the supplied native sphere data, Switch Toolbox,
  an ASTC encoder and external model compression helpers (`-ToolingHelpers`). These inputs
  and helper dependencies are not included in the source repository. The Champion capture
importer and its private asset receipt are also omitted; generated Champion resource
headers and converted game assets must be supplied by a separate local asset pipeline. Test-only SD logging
  and periodic memory sampling are absent.
  `tools/compose_resources.py` merges the three abilities' game data into one RomFS against your
  own TotK 1.2.1 dump, `tools/build_wheel_icons.py` makes the four wheel-page icons from your own
  BotW 1.6.0 dump, and `tools/build_release_restbl.py` builds the resource-size table for it. The
  `tools/build_champion_*.py` scripts generate the Champion headers and game data from your own
  TotK 1.2.1 and BotW 1.6.0 dumps: effects, save data, the updraft and lightning actors, Key Items
  and HUD icons, sounds, and Gale's pose (`tools/export_champion_pose.ps1` uses Switch Toolbox).
  The effect step also needs an XLink converter, which is not included.
  They use helper modules and a resource-table tool from the author's private repository, which
  are not included. The game data is not part of this source; it is only in the release archives.

## Tests

Each ability folder has its own host tests; see its README. This folder's `tests` check the
second wheel page (layout, page turns and picks), the native bomb policies and the Champions'
charges, triggers, blocking, healing, targets, pose, sound, save data and HUD; run
`tests/run_host_tests.ps1` with CMake and a C++23 compiler. doctest is fetched when it is not
vendored.

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## Credits

- The supplied native sphere data is the donor for bomb materials, trails and explosion routing.
- exlaunch by shadowninja108 and contributors.
- devkitPro.

## License

MIT - see `LICENSE` at the repository root. `NOTICE.txt` describes the exlaunch dependency and
the game files in the release archives.
