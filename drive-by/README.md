# Drive-by v0.9.0

Use your bow, sword and shield while holding a Zonai Steering Stick, and keep driving.

- **Bow:** draw it and Link takes his hands off the stick, holds the bow normally and turns his body
  and legs to aim where the camera points. Lower it and his hands go back to the stick.
- **Melee:** press Y to swing your weapon toward the camera, or toward the enemy you are locked on to.
  Hits land and Link stays on the stick.
- **Shield:** hold ZL to raise it and block. A blocked hit does not knock Link off the stick, and when
  locked on he keeps facing the enemy with the shield level.

Drive-by is the new name of Free Rein (bow only, v0.4.0). v0.9.0 adds melee and the shield, and ships
as patch files only, with no cheat file.

## Requirements

Tears of the Kingdom **1.2.1** only. Both files are keyed to 1.2.1's program, so other versions
ignore them and play normally.

## Installation

Close the game first. Choose the archive for where you play.

**Upgrading from Free Rein:** remove Free Rein first. Its files change the same places in the game,
and the two must not run together. On an emulator, delete or disable the `Free Rein` folder. On a
Switch, delete `atmosphere/contents/0100F2C0115B6000/cheats/9B4E43650501A4D4.txt`, or, if you added
Free Rein's lines to another mod's cheat file, remove the lines from `[Free Rein v0.4.0]` to the end
of that block.

### Emulators: `drive-by-v0.9.0-emulator.zip`

1. Open the game's mod folder. In most emulators, right-click the game and choose
   *Open Mod Data Location*; it is `load/0100F2C0115B6000/`.
2. Copy the `drive-by` folder from the archive into it, so you have:
   ```
   load/0100F2C0115B6000/drive-by/exefs/drive-by.pchtxt
   ```
3. Enable **drive-by** in the game's Add-Ons list.

The emulator applies `drive-by.pchtxt` when the game starts. The `exefs_patches` folder holds the
Switch copy of the same patch; emulators do not read it.

### Switch (Atmosphere): `drive-by-v0.9.0-switch.zip`

1. Copy the archive's `atmosphere` folder to the root of the SD card, so you have:
   ```
   atmosphere/exefs_patches/drive-by/9B4E43650501A4D4489B4BBFDB740F26AF3CF850.ips
   ```
2. Start the game. Atmosphere applies the patch at launch; there is nothing to switch on.

The archive also creates `atmosphere/exefs/drive-by.pchtxt`, the emulator copy of the patch.
Atmosphere does not read it; you can delete it.

## Compatibility

Drive-by uses no executable module slot, so it loads beside exlaunch mods. It conflicts with any
patch or cheat that changes the same places in the game's program. Offsets are from the start of the
main program; in the `.pchtxt` and `.ips` they are `0x100` higher.

- the list of actions the bow may run beside (`0xB7DC20` and `0xB7DCD0`-`0xB7DCEF`);
- the check that lets a melee weapon be drawn (`0x933174`);
- seven jumps from the game's code into Drive-by's code (`0x6877D8`, `0x82D540`, `0xE481B8`,
  `0x1D60274`, `0x1D6A6C4`, `0x1D6A728`, `0x219AD90`);
- the new code itself, in unused gaps between the game's functions (`0x5DE064`-`0x5E116F`).

Free Rein put its code in the empty space after the end of the game's code. Popular patches and
cheat sets (FPSLocker, the TotK Optimizer cheats) also write there. Drive-by leaves that space alone.
Before release its writes were checked against the 1.2.1 patches of 35 other sources (mods, cheat
collections and plugins, including the most-downloaded TotK mods that ship patch files); none of them
touch these places.

## Known issues

- Some strong attacks (seen with a large Black Moblin) still damage Link while the shield is up.
  Whether the same attack does this on foot without the mod has not been checked.
- Repeated swings repeat the same first swing instead of the left-right combo.
- No shield parry on the stick.

## Testing

v0.9.0 was played on 1.2.1 in Eden with the `.pchtxt` file: the bow, swings and shield all worked
from the stick. It was also played on a Switch with the `.ips` file, beside another mod's patch, and
worked there too.

## Uninstall

Disable or delete the `drive-by` folder on an emulator. On a Switch, delete
`atmosphere/exefs_patches/drive-by/`. The mod writes no save data.

## How it works

`drive-by.pchtxt` (an IPSwitch text patch, for emulators) and
`9B4E43650501A4D4489B4BBFDB740F26AF3CF850.ips` (IPS32, for Atmosphere) contain the same 371
instruction words.

1. **Allow the equipment on the stick.** The game checks which actions the bow and melee weapons may
   run beside, and refuses them while the Steering Stick is held. Ten words add the stick to those
   lists.
2. **Bow.** While the bow is drawn on the stick, the stick's animation update plays the lower-body
   animation the sand seal sled uses for bow aiming, so the legs turn toward the aim, and the hand
   placement on the handles is switched off the way the game does when you let go.
3. **Melee.** Pressing Y on the stick plays the weapon's own standing first swing on Link's upper
   body, turned toward the camera or the locked-on enemy, with its hits switched on for the swing.
4. **Shield.** ZL raises the shield from the stick routine. A blocked hit skips the reaction that
   would throw Link off, and while locked on his legs turn toward the enemy so the shield stays level.

The new code sits in 119 small unused gaps between the game's own functions, joined by short jumps.
