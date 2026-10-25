# Drive-by v0.12.2

Use your bow, melee weapons and shield while holding a Zonai Steering Stick, and keep driving.
Formerly Free Rein. Requires Tears of the Kingdom **1.2.1**.

## Controls

- **Bow:** aim and shoot normally. Link turns toward your aim. Press B to put the bow away with its animation.
- **Melee:** press Y to swing toward the camera or the enemy you are locked on to.
- **Charged attacks:** keep Y held through the first swing to charge, then release. Charging uses stamina.
  Sword charged attacks retain a sideways, mounted-style sweep; spears retain their repeated thrusts. Stamina recovers after the attack.
- **Shield:** hold ZL to raise it directly, without drawing a weapon first. Block while remaining on the stick.
- **Parry:** hold ZL and tap A with the usual timing against an incoming attack.
- **Rocket shield:** tap ZL to activate the rocket, then release it. Link stays with the Steering Stick and attached vehicle while the rocket boosts upward. Press B to cancel. Without the companion mod, rockets retain their normal duration.

## Changes in v0.12.1

- Fixed TKMM importing the text patch at the wrong addresses, which could prevent Drive-by from loading correctly.
- Verified manual coexistence on Citron with Blood Moon on Demand 1.0, UltraCam 3.0 Calamity, Infinite Ascend 1.1.0, and Dream Home Limit++ 2.0.
- Documented the scope/rocket-shield animation issue and its B-cancel/reboost workaround.

## Previous changes in v0.12.0

- Added tap-to-boost on the Steering Stick. Releasing ZL no longer causes the shield to stow during the active rocket session.
- The shield stays steady after release; B cancels the boost, and repeated use works on the same Steering Stick.

## Previous changes in v0.11.0

- Added compatibility with the full Infinite Rocket Shield mod, allowing continuous vehicle lift until cancellation.
- Fixed repeated rocket-shield use on the same Steering Stick becoming sideways and producing no thrust.
- Release both B and ZL after cancellation before raising the shield to boost again.

## Previous changes in v0.10.0

- Fixed the bow snapping onto Link's back without a stow animation.
- Fixed shield draw requiring a melee attack first.
- Added charged attacks and shield parrying on the stick.
- Rocket shields point upward and lift the Steering Stick and attached vehicle while Link stays aboard. Press B to cancel.
- Restored normal ground bow-camera framing while aiming from the stick.
- Fixed stamina failing to recover after charged attacks.

## Installation

Close the game before replacing files. Choose the archive for your platform.

**Updating Drive-by:** replace the existing Drive-by files with this version. Keep only one copy enabled.
**Upgrading from Free Rein:** remove or disable Free Rein first; its code patches overlap Drive-by.
On Switch, remove Free Rein's cheat file at
`atmosphere/contents/0100F2C0115B6000/cheats/9B4E43650501A4D4.txt`, or remove only the Free Rein block
if you combined it with other cheats.

### Emulator: drive-by-v0.12.2-emulator.zip

1. Open the game's mod folder (`load/0100F2C0115B6000/`).
2. Copy the archive's `drive-by` folder there. The active patch should be at
   `load/0100F2C0115B6000/drive-by/exefs/drive-by.pchtxt`.
3. Enable **drive-by** in the game's Add-Ons list and start the game.

The `exefs_patches` folder contains the Switch copy; the emulator does not use it.

### Switch (Atmosphere): drive-by-v0.12.2-switch.zip

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
  among the 37 surveyed third-party patch sources; that does not cover every possible mod combination.
- Added instructions occupy unused gaps inside the game's code (`0x5DE064` through `0x27B0D70`),
  leaving executable tail padding used by other patches alone. Every patched address is listed in the pchtxt.

## Infinite rocket-shield compatibility

Install the complete **Infinite Rocket Shield - Only When Fused To A Shield** mod separately, including its RomFS actor packs and registration tables. Drive-by does not include that mod or its game assets. A code patch alone is insufficient. Fuse a fresh rocket shield after installing the companion.

With both mods enabled, rocket thrust continues until cancellation on foot and on a Steering Stick. Tap ZL to start boosting; release ZL to keep the boost active. Press B to cancel. Release both B and ZL before raising the shield again. Ordinary rockets retain their normal duration without the companion. Other mods replacing the companion's registration tables require a compatible merge.

## Known issues and limits

- Opening the scope with R3 during an active rocket-shield boost on a Steering Stick can cause the shield to wobble between guard and stow. Press B to cancel, then boost again to restore the normal pose. Avoid opening the scope during a boost.
- Repeated normal swings repeat the first swing instead of alternating through the normal combo.
- Some strong attacks, observed with a large Black Moblin, still damage Link while the shield is up.
  The same attack has not been compared with unmodded on-foot blocking.
- Ground-slam shockwaves, charging effects and unusual weapon/fuse interactions are not fully verified.

## Testing

The v0.12.1 text patch was tested on Citron with TotK 1.2.1, the complete Infinite Rocket Shield companion, Camera Guard Pose Fix, Blood Moon on Demand 1.0, UltraCam 3.0 Calamity, Infinite Ascend 1.1.0, and Dream Home Limit++ 2.0. Drive-by worked, the UltraCam menu opened, and both combined boots completed successfully. This does not verify every feature of the other mods.

The actual TKMM 2.1.6 patch parser imports and exports all 2,246 corrected instruction words exactly. The old format reproduces a 256-byte displacement. The reporter's full TKMM merged installation has not been tested. The corrected text applies the same accepted gameplay instructions as v0.12.0; the Switch IPS is byte-identical.

The previous v0.9.0 release was tested on Eden and a physical Switch. **v0.12.1 has not had a separate physical Switch or Android play-test.** The Switch archive is provided with that limitation.

## Uninstall

Disable or remove the `drive-by` folder on your emulator. On Switch, remove
`atmosphere/exefs_patches/drive-by/`. The mod does not change save data.

## Patch source

The public `patches/drive-by.pchtxt` is the version-locked patch source. Both formats contain the same
2,246 instruction words, with added code placed in 745 unused alignment gaps. The mod changes how the
game selects and runs its existing actions and animations; it distributes no game assets.

Source: https://github.com/afiaka87/totk-mods/tree/main/drive-by
License: MIT.


## Changes in v0.12.2

Refactors the patch builder into named verification, layout and output operations. Both
patch files are byte-for-byte identical to v0.12.1. Gameplay, memory usage, compatibility
and known issues are unchanged; the existing gameplay results apply to these same bytes.
