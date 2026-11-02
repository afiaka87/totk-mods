# Ultimate Ascend compatibility

Ultimate Ascend v0.16.0 includes an adapted version of LordCameron's Ascend Through Depth Ceiling
free v1.0.5. The shared upward hook preserves its acceleration when Descend does not own the event.
On the experimental shrine-restricted variant, the original native paths are restored inside shrines.

Disable or remove separate Ascend Through Depth Ceiling, Infinite Ascend and older Descend patches.
If TKMM bundles one of them, remove that selection and rebuild its output. Use only one Ultimate Ascend
variant. Other mods editing the same native instructions or reused padding can conflict.

All nine game builds have independent instruction/layout checks. The original free assembly is compared
against the ported routines; its upward paths and Descend's shared-hook bypass are tested after composition.
The local known-write survey is incomplete, particularly on 1.4.3. Zero known overlap is not a guarantee
of compatibility with every mod. Paid/Zonaite versions of Ascend Through Depth Ceiling are untested.

The standard builds were tested on Switch with game versions 1.1.2 through 1.4.3. The 1.0.0 and 1.1.0
reports came from Ryubing and retain the known issues in README.md. Shrine-restricted downloads remain
experimental, with offline validation but no observed play-test.

Ascend Through Depth Ceiling: https://gamebanana.com/mods/723719
Its original MIT license and notices are retained as ASCEND-LICENSE.txt and ASCEND-NOTICE.txt.
