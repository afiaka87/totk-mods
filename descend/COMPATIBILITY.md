# Descend v0.15.1 compatibility edition

Combines Descend with LordCameron's Ascend Through Depth Ceiling **free v1.0.5**
for **TotK 1.2.1** (build 9B4E43650501A4D4489B4BBFDB740F26AF3CF850).
It preserves Descend's drop/return and the other mod's upward acceleration.
No Zonaite is consumed. This is a replacement for the two separate patches.
Standalone Descend v0.15.0 remains available and unchanged.

## Install

Close the game first. Disable/remove standalone Descend (including descend-patch),
Ascend Through Depth Ceiling and Infinite Ascend before enabling this edition.
If TKMM includes either original patch, remove those selections and rebuild its output.
Do not leave old copies active alongside this package. Other unrelated mods can stay.

- Emulator: extract this archive into the game's mod directory and enable
  `descend-compat-free`. It contains `exefs/descend-compat-free.pchtxt`.
- Switch: copy `descend-compat-free/exefs_patches/descend` from the archive into
  `atmosphere/exefs_patches/`, replacing the existing Descend IPS after backing it up.
  Do not copy the emulator pchtxt to the Switch. Remove any separate Ascend Through
  Depth Ceiling IPS. Disable an installed Infinite Ascend subsdk4 without removing
  another mod's files or the shared main.npdm.

While aiming Ascend, click the left stick to toggle Descend. A at a green marker
travels down; at the exit preview, A drops and B returns. Toggle back for Ascend.
The free Ascend mod accelerates long open-air climbs after five seconds, up to 8x.

## Testing and limitations

The ordered combination passed Citron and Switch tests. The corrected shared hook
then passed the Switch acceleration, Descend drop and return tests. The package
contains those exact accepted patch bytes. Offline tests compare 56 upward cases
with the original acceleration routine and verify four downward bypass cases.
The paid/Zonaite version and other game versions are not supported by this edition.
Slopes can obscure Link's legs; not every terrain exit has been tested.
The friend's earlier always-red report remains unexplained. Do not infer that two
unmodified standalone packages work in every load order.

## Source and attribution

Descend source and the compatibility composer are in the public source repository.
`tools/compose_compat_free.py` takes the exact free-v1.0.5 public pchtxt, the accepted
Descend patch06 pchtxt and its retained layout. It verifies input hashes and redirects
the existing native fallback to the community acceleration hook. It does not recompile
or replace Descend's geometry, effects or camera code. `tools/test_compat_free.py`
executes the composed ARM paths using Unicorn and the retained layout/ELF.
These developer tools require the private build environment described by Descend's
source documentation; this folder is not a complete standalone game-module toolchain.

Ascend Through Depth Ceiling: LordCameron, https://gamebanana.com/mods/723719.
Its original MIT LICENSE and NOTICE are included as ASCEND-LICENSE.txt and
ASCEND-NOTICE.txt. Descend is by Clay Mullis. See NOTICE.txt for Descend's terms.
