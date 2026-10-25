# Infinite Ascend v1.1.2

## Changes in v1.1.2

Uses the shared 8 MiB loader reservation so installing this package does not overwrite the combined-mod memory fix.

The six-mod combination of Infinite Ascend, Fleet Sync, Self Recall (Switch), Zonai Hookshot with Arrowbound, BotW Abilities and Zonai Survey passed a physical Switch 1.2.1 session. Other game versions and combinations have not had a new test of the 8 MiB setting.

Extends the Ascend ability's reach from about 20 m to 10,000 m, so Link can rise
through mountains, canyon roofs, and sky islands overhead. Target checks stay
native: the game still validates the surface, the space above it, and the route;
the mod only extends how far the upward search reaches, slightly relaxes two
local surface-shape checks (steep/rough ceilings that vanilla rejects), and
scales the target marker so it stays readable at long range. No buttons, menus,
or HUD elements are added , Ascend works exactly as it always did, just farther.

## Requirements

- The Legend of Zelda: Tears of the Kingdom **1.0.0, 1.1.0, 1.1.2, 1.2.0,
  1.2.1, or 1.4.0–1.4.3**. The same download serves all nine versions.
  The executable checks twelve
  original instructions for a unique supported version before installing
  any hook; if those guarded words do not match, it leaves all hooks off.
- No other files, mods, or save changes required. Contains no game assets.

## Install (emulator)

1. Right-click the game in your emulator and open the mod/load directory
   (`load/0100F2C0115B6000/`).
2. Extract the zip there, producing `infinite-ascend/exefs/{main.npdm, subsdk4}`.
3. Enable **infinite-ascend** in the game's properties / add-ons list.

Developed and tested on an emulator. Other emulators that support
executable mods in the load directory use the same layout.

## Install (Switch hardware , untested by the author)

Copy the two files in `exefs/` to
`atmosphere/contents/0100F2C0115B6000/exefs/`. This is the standard layout for
exlaunch-based mods, but this mod has not been tested on hardware.

## Compatibility

- Do not enable another mod that supplies `subsdk4`. Other executable slots are separate; this does not guarantee hook or memory compatibility.
- Known accepted quirk: on very irregular ceilings the relaxed surface check may
  occasionally accept a spot vanilla would refuse.

## Payload

- `exefs/subsdk4` , SHA-256 `EE0ED01250F9CDB02DD420791DC4662C4D9645BC90BC281EFF3C310E17A649E6`.
- `exefs/main.npdm` , SHA-256 `CA38B1AC9B3F152CBFDA3AEA89C6BC14B80DA525787CD6B3F6CCFCC45F13DCBE`.

## Changes in v1.0.1

Removes an unused symbol table and its lookup code left over from a retired
diagnostic overlay (about 4 KB smaller). No gameplay change.

## Slot-only update in v1.0.2

Moves the executable from subsdk9 to **subsdk4**. Gameplay is unchanged.
Remove this mod's old `exefs/subsdk9` when upgrading. On Switch, preserve any
slot 9 file belonging to another mod. This change separates executable filenames;
it does not claim new compatibility testing.

## Changes in v1.1.0

Adds guarded profiles for 1.0.0, 1.1.0, 1.1.2, 1.2.0, and 1.4.0–1.4.3.
Reach, leniency, controls, and marker behavior are unchanged. The 24 MiB
package was also tested successfully on 1.2.1 in Eden.

Downloads are `infinite-ascend-v1.1.2-emulator-subsdk4.zip` and
`infinite-ascend-v1.1.2-switch-subsdk4.zip`. The Switch archive wraps the
same payload in `0100F2C0115B6000/`
for extraction under `atmosphere/contents/`.

## Changes in v1.1.1

Refactors marker snapshot ownership and fixed reach queries. Reach, controls, validation,
marker behavior and the nine game profiles are unchanged. The module still maps 204 KiB;
that version retained 24 MiB. The current release uses 8 MiB as described above.

The refactor passes nine host tests, 764,970 native behavior comparisons and the ARM build.
Native call order, atomic publication and floating-point operations were checked against
the accepted build. The refactor subsequently passed user-reported gameplay checks; the current combined Switch 1.2.1 acceptance is recorded above.
