# Zonai Survey v0.3.0

Press **ZL + D-pad Up** to send a scan across the visible landscape and reveal collectible names and icons.

## Choose a build

- **Regular:** approximately 440 m range and a 3-second cooldown.
- **Constrained:** 180 m range and a 7-second cooldown.

Both use the same 100-degree cone and surface detail. Cooldown starts when a scan activates. Trying again too soon plays a short refusal sound without extending the wait. Install only one flavor.

The surface lines follow the scene depth. Names and icons stay visible over foreground objects, including Link and hills. Labels use Rodin bold with a thin outline and a soft shadow. When labels crowd together, farther ones show only their icon, and names fade in and out instead of blinking.

## Installation

Supports Tears of the Kingdom 1.0.0, 1.1.0, 1.1.2, 1.2.0, 1.2.1 and 1.4.0–1.4.3. Each archive covers every supported version; Survey detects the version at startup and stays inactive on any other. Close the game first.

For an emulator, extract `zonai-survey-emulator-regular-v0.3.0-subsdk9.zip` or `zonai-survey-emulator-constrained-v0.3.0-subsdk9.zip` into the game's mod directory and enable **zonai-survey**.

For Switch, extract `zonai-survey-switch-regular-v0.3.0-subsdk9.zip` or `zonai-survey-switch-constrained-v0.3.0-subsdk9.zip` under `atmosphere/contents/`. Merge the included `0100F2C0115B6000` directory; do not replace other mods' files.

Each archive contains `exefs/subsdk9` and `exefs/main.npdm`. Emulator and Switch packages use the same executable for each flavor. Only one mod may occupy subsdk9.

This version needs no separate font files or ROMFS assets. Remove only Survey's obsolete font/primitive files when upgrading, and only if no other mod uses them. Back up the previous installation.

## Changes in v0.3.0

- Labels use Rodin bold with a thin outline and a soft shadow, aligned to screen pixels.
- On 1.2.1, labels draw with the game's interface. The HUD, pause menu and dialogue cover them, and they follow the camera without trailing during pans.
- On the other versions, labels still draw in the 3D scene as in v0.2.0, with the new font and outline.
- When labels crowd, a farther label shows only its icon. A hidden name waits until its space stays clear, and names fade in and out instead of blinking.

The v0.3.0 Regular build was tested in Citron on 1.2.1. It has not yet been run on a physical Switch or on other game versions. Constrained differs only in its range and cooldown settings and was not booted separately.

## Changes in v0.2.0

- Supports Tears of the Kingdom 1.0.0, 1.1.0, 1.1.2, 1.2.0, 1.2.1 and 1.4.0–1.4.3. v0.1.5 required 1.2.1.
- On 1.4.x, the game no longer closes at launch with the mod installed: the module header now names the SDK version that the 1.4.x game's loader reads.
- Removes diagnostic logging and startup diagnostics.

The version port passed combined sessions with Self Recall and Phantom Foothold on a physical Switch running each of the nine supported versions. The final v0.2.0 Regular build passed on a physical Switch running 1.4.3 and in Eden on 1.2.1. Constrained differs only in its range and cooldown settings and was not booted separately.

## Changes in v0.1.5

- Replaced native font initialization with a compact atlas for icons and Rodin regular labels.
- Deferred drawing-resource creation until content is visible and reused bounded GPU buffers.
- Preserved Glideshot's rendering when both mods share the same drawing callback.
- Kept labels visible over foreground geometry and retained camera-relative scanning.
- Removed obsolete font and primitive-renderer code; first-party source is now MIT.

The atlas and combined-mod fixes were accepted on physical Switch; the atlas visuals were also checked in Eden. Release cleanup is checked separately against that accepted build.

## Limitations

- Surface lines use visible scene depth, not hidden or off-screen surfaces.
- Low surfaces remain blue by height rather than material.
- Dense grass may briefly cost about 3-4 fps on Switch; a locked 30 fps is not guaranteed.
- Collectible names and positions come from the 1.2.1 data; exact placement on other versions has not been compared.
- On versions other than 1.2.1, labels draw in the 3D scene, where they can look slightly dimmer than the game's HUD text. That path was not re-checked with the new font.
- A smoke test does not establish exhaustive scene-transition or long-session compatibility.

## Source and tests

The public tree is first-party source, not a complete Switch build environment. Build dependencies include exlaunch, devkitA64 and compatible engine declarations. The `runtime-support/include` folder supplies the MIT helper headers.

Run `tests/run_host_tests.ps1` for host checks. CMake uses local doctest when available, otherwise fetches v2.4.11. Portable tests do not require game data. With locally generated collectible tables present, `SURVEY_TEST_GAME_DATA=ON` additionally verifies every real table record; those inputs are intentionally absent from the public tree.

Atlas pixels, font containers and collectible tables are generated locally from an owned game and are not public source. They are not relicensed. No guarantee is made that the Switch target builds from this source-only checkout without those inputs and external tooling.

The atlas-baking script also expects local font-container decoding helpers and Pillow. Those helpers and the game inputs are not part of this source-only checkout; the portable host tests do not invoke them.

## A note on the code

Is this partially vibe coded? Yes. It's a mod so I don't hold myself to the same standards as I do
with my professional work. If that bothers you, I apologize sincerely. But note that the license is
highly permissive (MIT) and I hope this adds to the community.

## License

First-party source is MIT; see LICENSE. The compiled executable includes external GPL-2.0 exlaunch code. See NOTICE for dependency and asset notices.
