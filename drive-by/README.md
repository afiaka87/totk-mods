# Drive-by v0.10.0

Use your bow, melee weapons and shield while holding a Zonai Steering Stick, and keep driving.
Formerly Free Rein. Requires Tears of the Kingdom **1.2.1**.

## Controls

- **Bow:** aim and shoot normally. Link turns toward your aim. Press B to put the bow away with its animation.
- **Melee:** press Y to swing toward the camera or the enemy you are locked on to.
- **Charged attacks:** keep Y held through the first swing to charge, then release. Charging uses stamina.
  Charged attacks use a sideways, mounted-style motion while Link stays on the stick.
- **Shield:** hold ZL to raise it directly, without drawing a weapon first. Block while remaining on the stick.
- **Parry:** hold ZL and tap A with the usual timing against an incoming attack.
- **Rocket shield:** raise a rocket shield to release the stick and lift off. This lifts Link, not the vehicle.

## Changes in v0.10.0

- Fixed the bow snapping onto Link's back without a stow animation.
- Fixed shield draw requiring a melee attack first.
- Added charged attacks and shield parrying on the stick.
- Fixed rocket shields leaving Link stuck on the stick with the shield sideways.

## Installation

Close the game before replacing files. Choose the archive for your platform.

**Updating Drive-by:** replace the existing Drive-by files with this version. Keep only one copy enabled.
**Upgrading from Free Rein:** remove or disable Free Rein first; its code patches overlap Drive-by.
On Switch, remove Free Rein's cheat file at
`atmosphere/contents/0100F2C0115B6000/cheats/9B4E43650501A4D4.txt`, or remove only the Free Rein block
if you combined it with other cheats.

### Emulator: drive-by-v0.10.0-emulator.zip

1. Open the game's mod folder (`load/0100F2C0115B6000/`).
2. Copy the archive's `drive-by` folder there. The active patch should be at
   `load/0100F2C0115B6000/drive-by/exefs/drive-by.pchtxt`.
3. Enable **drive-by** in the game's Add-Ons list and start the game.

The `exefs_patches` folder contains the Switch copy; the emulator does not use it.

### Switch (Atmosphere): drive-by-v0.10.0-switch.zip

1. Copy the archive's `atmosphere` folder to the root of your SD card.
2. Check that the patch is at
   `atmosphere/exefs_patches/drive-by/9B4E43650501A4D4489B4BBFDB740F26AF3CF850.ips`.
3. Start the game. No cheat needs enabling.

The archive also includes `atmosphere/exefs/drive-by.pchtxt`, the emulator copy. Atmosphere ignores it.

## Compatibility

- **TotK 1.2.1 only.** The files are keyed to its executable build ID.
- **ExeFS code patches only:** no RomFS replacements, executable module or subsdk slot.
- Free Rein and older Drive-by copies must not run alongside this version.
- Other mods can conflict if they patch the same executable addresses. No overlapping writes were found
  among the 35 surveyed third-party patch sources; that does not cover every possible mod combination.
- Added instructions occupy unused gaps inside the game's code (`0x5DE064` through `0x5F04EF`),
  leaving executable tail padding used by other patches alone. Every patched address is listed in the pchtxt.

## Known issues and limits

- Repeated normal swings repeat the first swing instead of alternating through the normal combo.
- Some strong attacks, observed with a large Black Moblin, still damage Link while the shield is up.
  The same attack has not been compared with unmodded on-foot blocking.
- Ground-slam shockwaves, charging effects and unusual weapon/fuse interactions are not fully verified.

## Testing

This update was tested on Citron with TotK 1.2.1: bow stow, direct shield draw, charged attacks,
parrying and rocket-shield lift worked. The sideways charged-attack animation is retained.
The pchtxt and IPS32 files apply identical code changes, and 101 offline ARM checks pass.

The previous v0.9.0 release was tested on Eden and a physical Switch. **v0.10.0 has not had a separate
physical Switch or Android play-test.** The Switch archive is provided with that limitation.

## Uninstall

Disable or remove the `drive-by` folder on your emulator. On Switch, remove
`atmosphere/exefs_patches/drive-by/`. The mod does not change save data.

## Patch source

The public `patches/drive-by.pchtxt` is the version-locked patch source. Both formats contain the same
1,104 instruction words, with added code placed in 363 unused alignment gaps. The mod changes how the
game selects and runs its existing actions and animations; it distributes no game assets.

Source: https://github.com/afiaka87/totk-mods/tree/main/drive-by
License: MIT.
