# Straight to Gameplay v0.2.0

Start the game and land in your most recent save with no button presses. After the startup logo the
loading screen appears and your latest save loads. The title backdrop, the title menu, the save
list and the confirmation prompt are all skipped.

It is meant for people who restart the game often, such as mod developers.

## Requirements

- Tears of the Kingdom **1.2.1**. The files are rebuilt from 1.2.1's own and are not tested on
  any other version.
- At least one save. With no save the game starts the normal opening.
- An updated resource size table (RESTBL). See below.

## Installation

Close the game first.

### Emulators

1. Open the game's mod folder. In most emulators, right-click the game and choose
   *Open Mod Data Location*; it is `load/0100F2C0115B6000/`.
2. Copy the `Straight to Gameplay` folder from the archive into it, so you have:
   ```
   load/0100F2C0115B6000/Straight to Gameplay/romfs/Sequence/BeforeTitle.module.ainb
   load/0100F2C0115B6000/Straight to Gameplay/romfs/Sequence/NormalTitle.module.ainb
   ```
3. Enable **Straight to Gameplay** in the game's Add-Ons list.

### Switch (Atmosphere)

Copy the `romfs` folder from inside `Straight to Gameplay` into
`atmosphere/contents/0100F2C0115B6000/`. This layout is the standard one but v0.2.0 was only
played on an emulator.

### Resource size table

Both files differ in size from the originals, and `NormalTitle.module.ainb` is larger than the
size the game's table reserves for it. The table needs an entry for each file that is at least as
large as the new file:

| File | Size in this mod | Table entry used in testing |
|---|---|---|
| `Sequence/NormalTitle.module.ainb` | 17384 | 21480 |
| `Sequence/BeforeTitle.module.ainb` | 10886 | 14982 |

A mod manager that rebuilds the table when it merges mods does this for you. Otherwise run a
RESTBL tool over your mod set after adding this mod. The archive does not include a table,
because a whole table would overwrite the one your other mods need.

## What changes while it is enabled

- There is no way to reach New Game, Options or an older save from the title screen.
- "Return to title" from the pause menu loads the latest save again.
- If the game returns to the title with no save on the system at all (a new game that never
  saved), it waits on a menu that is never drawn. Disable the mod in that case.

Disable the mod to get the title menu back.

## Compatibility

Uses no executable module slot, patch or cheat, so it loads beside code mods. It conflicts with
any mod that replaces `Sequence/NormalTitle.module.ainb` or `Sequence/BeforeTitle.module.ainb`.

## Testing

v0.2.0 was played on 1.2.1 in Eden with several other mods enabled: boot went from the startup
logo to the loading screen and into the latest save, and the game played normally from there.
"Return to title" and the Switch were not tested.

## Uninstall

Disable or delete the `Straight to Gameplay` folder. The mod writes no save data.

## How it works

The game's boot and menu flow is described by sequence graph files. This mod edits two of them.

- `NormalTitle.module.ainb` is the title menu. When a save exists it no longer waits for a
  button. It selects the most recent save, the same way the game-over "Continue" does, and goes
  to the step the game runs after you confirm loading a save.
- `BeforeTitle.module.ainb` is the boot step that prepares the title screen. It no longer loads
  the title backdrop map or opens the menu, and keeps the one-time setup that step performs.

## Notice

The two files are edited copies of files from Tears of the Kingdom. They are useful only with
your own copy of the game. The Legend of Zelda is a trademark of Nintendo; this mod is not
affiliated with or endorsed by Nintendo.
