# Infinite Ascend v1.2.0

Extends Ascend's reach from about 20 m to 10,000 m, relaxes two local surface-shape
checks, and scales the red/green target marker so it stays readable at long range.
Use Ascend normally; no extra buttons, menus or HUD elements are added.

## Changes in v1.2.0

On **TotK 1.2.1**, Ascend can find more distant sky-island targets using the game's
terrain map when their collision has not loaded. Link checks for physical support
when he arrives. B cancels during the open-air approach instead of waiting until
he reaches the island. Tall ascents are faster, using the full rise from the
starting point to the destination:

| Full vertical rise | Upward speed |
| --- | --- |
| At or below 150 m | 1x |
| Above 150 m | 1.5x |
| Above 300 m | 2x |
| Above 500 m | 3x |
| Above 1,000 m | 6x |
| Above 1,500 m | 7x |
| Above 2,000 m | 7.5x |

The highest qualifying tier applies. Short ascents, entry/exit animations and
return speed stay native. Ascend keeps the game's fixed camera throughout.

The other eight supported versions retain v1.1.2's reach, surface checks and marker
behavior. They do not receive the new targeting, early cancellation or speed tiers.

## Requirements and testing

- Supports TotK **1.0.0, 1.1.0, 1.1.2, 1.2.0, 1.2.1, 1.4.0, 1.4.1, 1.4.2 and 1.4.3**.
  One download serves all nine versions. Original instructions identify each
  version before hooks install; the 1.2.1 extension has additional guards.
- Contains no game assets and requires no save changes.
- The base feature has prior emulator acceptance on all nine versions. The new
  1.2.1 behavior was tested in Citron and on Switch, including tall ascent, B-cancel
  and ordinary short ascent. The release cleanup removes travel diagnostics;
  host and compiled ARM checks pass. These exact cleaned bytes have no separate boot.
- Great Sky Island is an accepted route. Not every sky island or terrain edge case
  has been tested. Irregular ceilings can occasionally accept a spot vanilla rejects.

## Install (emulator)

1. Close the game and open its mod/load directory (`load/0100F2C0115B6000/`).
2. Extract `infinite-ascend-v1.2.0-emulator-subsdk4.zip` there. The result is
   `infinite-ascend/exefs/{main.npdm, subsdk4}`.
3. Enable **infinite-ascend** in the game's add-ons list.

## Install (Switch)

Close TotK. Extract `infinite-ascend-v1.2.0-switch-subsdk4.zip` under
`atmosphere/contents/`, producing `0100F2C0115B6000/exefs/{main.npdm, subsdk4}`.
Back up existing files before replacing them. Both downloads contain the same payload.

## Compatibility

- Do not enable another mod that supplies `subsdk4`. Separate slots alone do not
  guarantee compatible hooks or memory settings.
- `main.npdm` is shared with other executable mods. This package keeps the 8 MiB
  loader reservation used by v1.1.2 and the accepted Switch setup.
- Infinite Ascend and Descend have passed combined gameplay testing on 1.2.1.
- When upgrading from a slot 9 release, remove only Infinite Ascend's old
  `exefs/subsdk9`; preserve a slot 9 file belonging to another mod.

## Source and tests

The public folder contains first-party source and tests, not a complete module
build environment. It omits the exlaunch framework, SDK headers, toolchain and build
glue. First-party source is MIT licensed; the compiled module includes exlaunch's
GPL-2.0 runtime. See the repository's LICENSE and NOTICE.

The module uses devkitA64, exlaunch and the TotK SDK headers. Compile all `.cpp` files
under `src/program/`, with `src`, `src/program`, `src/pure`, the exlaunch source/lib
directories and SDK include roots on the include path. The consumer templates use
module name `zonai-ascend`, `EXL_USE_FAKEHEAP`, no `EXL_DEBUG`, HeapSize `0x10000`,
JitSize `0x4000`, InlinePoolSize `0x1000`, LogBufferSize `512`, an empty relocation
table, `TOTK_VERSION=121`, and title ID `0100F2C0115B6000`. This does not restrict
runtime selection to 1.2.1; the source contains the nine guarded profiles.

The floating-register bridge depends on exlaunch's assembly context layout and its
corrected `S[]` accessor (upstream issue 28 / PR 31). The current bridge reads general
registers after the 0x200-byte floating block. Reconcile that layout before using a
different framework revision; stock headers alone are not a verified replacement.

Run host tests with CMake, Ninja and a C++23 compiler:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_host_tests.ps1
```

The Windows runner finds MSVC. Standalone tests fetch pinned doctest v2.4.11.
`zonai_ascend_verify_main <main image> <game version>` checks a privately dumped
executable; no game image is included. `verify_tall_speed_native.py` checks a built
ARM ELF and your 1.2.1 image using Unicorn, pyelftools and Capstone. Its docstring
contains the command. `verify_runtime_imports.ps1 -Elf <file> -Nm <devkitA64 nm>`
checks the linked module's imports.

## Earlier releases

v1.1.2 adopted the shared 8 MiB loader reservation. v1.1.1 simplified marker state
and reach queries. v1.1.0 added the other eight guarded game profiles. v1.0.2 moved
the module from slot 9 to slot 4. v1.0.1 removed an unused diagnostic symbol table.
These changes remain in v1.2.0.
