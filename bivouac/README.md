# Bivouac v0.26.3

## Changes in v0.26.3

Uses the shared 8 MiB loader reservation so installing this package does not overwrite the combined-mod memory fix.

The six-mod combination of Infinite Ascend, Fleet Sync, Self Recall (Switch), Zonai Hookshot with Arrowbound, BotW Abilities and Zonai Survey passed a physical Switch 1.2.1 session. Other game versions and combinations have not had a new test of the 8 MiB setting.

Build camps on cliff faces and over open water. While Link is climbing, eat one of three
ingredients and a shelter assembles under him: a stone deck, a wooden roof that keeps the rain
off, and a lit cooking pot. Camps stay where you built them, rebuild as you come back, and each
one gets its own icon on the map that you can travel to like a shrine.

## Camp tiers

| Eat while climbing | You get |
|---|---|
| Hearty Truffle | **Bivy**: a small deck, roof and lit cooking pot |
| Big Hearty Truffle | **Camp**: a larger deck with a tent, bedroll, banner and brightbloom beacon |
| Big Hearty Radish | **Basecamp**: the largest deck, plus a construction yard with reusable wood, stone and metal parts, a Zonai spring, and the Beedle+ camp shop |

Eat the same item away from a cliff, facing open water, and you get a **Mother Base**: a floating
version of that tier.

The item is used up when the camp is built. Use the fire or pot to cook, warm up, or pass time,
as at any other campfire.

## Using it

- **Build on a cliff:** climb somewhere with room for a deck and eat the item for the tier you
  want. The trigger works while climbing and for about a second after letting go of the wall.
- **Build over water:** away from a cliff, face a clear, large, open stretch of water and eat the
  item. A Mother Base appears out on the water; swim, sail or glide to it. Shallow, cramped,
  uneven or blocked water is refused.
- **Travel:** open the map, move the cursor onto a camp icon and press **A**. The map closes and
  Link travels there.
- **Take a camp down:** stand within 10 m of it, hold **ZL + ZR** and tap **D-pad Left**. The
  item is not returned.
- **Spacing:** a new cliff camp must be at least 160 m from every other camp, a Mother Base at
  least 250 m from every other camp. Camps within 160 m also show on the minimap, so move
  until none is on the minimap and the next camp will be accepted.

Eating these items on ordinary ground does nothing special: the food effect applies and nothing
is built.

## Sounds

Bivouac has no on-screen text. It uses the game's own interface sounds: a short chime when a camp
is built, an error tone when a placement is refused and the item is returned, the map-marker tone
when a camp travel starts, and a sign tone when a camp is taken down.

## Requirements and installation

Requires Tears of the Kingdom **1.2.1**, build `9B4E43650501A4D4`.

**Eden or another emulator:** extract `bivouac-v0.26.3-emulator-subsdk7.zip` and copy its `Bivouac`
folder into the game's mod folder (in Eden: right-click the game, **Open Mod Data Location**).
Enable **Bivouac** in the game's add-on list.

**Switch (Atmosphere):** extract `bivouac-v0.26.3-switch-subsdk7.zip` and copy its `0100F2C0115B6000`
folder into `atmosphere/contents/` on the SD card.

Both archives contain the same files: `exefs/subsdk7`, `exefs/main.npdm`, and seven files under
`romfs/`.

## Compatibility

- Bivouac occupies the `subsdk7` code slot. Do not enable another mod that supplies `subsdk7`;
  two of them can stop the game from booting.
- It replaces the Common UI layout archive (for the camp map icon), the save-data schema
  (`GameDataList`), the English text archive, two actor tables and the resource-size table. Mods
  that replace any of those files conflict unless their changes are merged. If you use another
  mod that ships its own resource-size table, merge the tables with a tool such as TKMM.
- Camp records live on the SD card (`totk_bivouac/sites.bin`), not in the save file, so all
  save slots share the same camps. The mod's log is written next to it.

## Known issues

- The Beedle+ shop at a basecamp sells normally, but selling to him does nothing; his name
  says so in game.
- On rare occasions the game drops Link below the deck right after a camp travel; Bivouac lifts
  him back onto it.
- The roof is an ordinary wooden object and can burn or be chopped. The camp rebuilds the next
  time you come back.
- The slabs that seal a cliff camp against the rock can clip into very uneven cliffs.

## Building from source

This folder contains only the mod's own code. It is **not** a complete project and comes with no
guarantee that it builds or works as-is; you set up the toolchain and framework yourself.

- Toolchain: devkitPro devkitA64.
- Framework: [exlaunch](https://github.com/shadowninja108/exlaunch) (GPL-2.0, not included).
  Known-good base: commit `f698816d`. Apply the `InlineFloatCtx` fix from exlaunch issue #28 /
  PR #31; the map-travel inline hook uses that context.
- Layout: put `src/program/` and `src/support/` in an exlaunch project, and add `src/lib/program/`'s
  two files (`sd_logger.hpp`, `sd_logger.cpp`) next to exlaunch's own `source/program/` files.
  Include roots: `src`, `src/lib`, `src/program`, `src/support`, and exlaunch's `source`,
  `source/lib` and `source/nn`.
- Use exlaunch's own `source/program/{setting,loggers,version,offsets}.hpp`. In `setting.hpp` set
  `EXL_MODULE_NAME "bivouac"`, keep `EXL_USE_FAKEHEAP`, remove `EXL_DEBUG`, and use
  `HeapSize 0x10000`, `JitSize 0x5000`, `InlinePoolSize 0x2000`, `LogBufferSize 512`. In
  `loggers.hpp` add `bivouac::log::SdFileLogger` (from `program/sd_logger.hpp`) after
  `exl::log::SvcLogger`. Leave the reloc table in `offsets.hpp` empty.
- Compile definitions: `TOTK_VERSION=121`, `TOTK_121=1`. Program ID `0100F2C0115B6000`, module
  `subsdk7`.
- The `romfs/` files are edited game data and are not part of this source folder; they are only
  in the release archives.

## Tests

- `tests/run_host_tests.ps1` builds and runs the eight host test programs (placement policy, water
  camp solver, camp record format, refunds, raycasts, camp geometry, assembly, travel arrival). It
  needs CMake, Ninja and a C++23 compiler on `PATH`. GCC, Clang and MSVC are supported;
  use `-Compiler cl` from a Visual Studio developer shell for MSVC, or pass a compiler folder
  with `-ToolPath`.

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

## Slot-only update in v0.26.1

Moves the executable from subsdk9 to **subsdk7**. Gameplay is unchanged.
Remove this mod's old `exefs/subsdk9` when upgrading. On Switch, preserve any
slot 9 file belonging to another mod. This change separates executable filenames;
it does not claim new compatibility testing.

The slot migration remains in v0.26.2.

Downloads: `bivouac-v0.26.3-emulator-subsdk7.zip` and `bivouac-v0.26.3-switch-subsdk7.zip`. The Switch archive wraps the same payload in `0100F2C0115B6000/` for extraction under `atmosphere/contents/`.

## Changes in v0.26.2

The frame update now names its climb observation, SD-card readiness and camp reporting steps.
Gameplay, controls, camp records and assets are unchanged. The compiled instructions and
allocated data match v0.26.1 exactly; only the build identifier differs. The module still maps
480 KiB and uses the same 16 MiB process resource setting. All eight host suites and 522,176
events in the existing independent runtime comparison pass. No new gameplay test was requested
for this source cleanup; the exact native comparison preserves the prior release evidence.

Executable SHA-256 (`subsdk7`): `138834afb052f5ebb7418e33d6686ba0b5aa6db24e12b52d17e631687629a5ba`.
