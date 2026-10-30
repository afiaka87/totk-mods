"""Verify the native-bloom routing against Clay's local relocated TotK 1.2.1 executable."""
# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
import argparse
import hashlib
import json
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
data = args.binary.read_bytes()

def word(address):
    return struct.unpack_from("<I", data, address)[0]

def expect(address, instruction):
    assert word(address) == instruction, f"Unexpected instruction at {address:#x}"

def call(address, destination):
    instruction = word(address)
    displacement = instruction & 0x3ffffff
    if displacement & 0x2000000:
        displacement -= 0x4000000
    assert instruction >> 26 == 0b100101 and address + 4 * displacement == destination

# Extension phases: mask, monochrome, auxiliary, composite/release.
phases = [0xc30cb8 + value * 4 for value in data[0x381d862:0x381d866]]
assert phases == [0xc30cb8, 0xc30f14, 0xc30f50, 0xc30f98]
call(0xc30efc, 0xc3257c)
call(0xc30f3c, 0xc31ddc)
call(0xc30fcc, 0xc31afc)
expect(0xc30fd8, 0xf940af21)  # Mask release follows the composite.
expect(0xc30fe0, 0xf940b321)

# ModelScene::drawPostEffects invokes phase 1, bloom, then phase 3 on the same context.
expect(0xc59854, 0x52800022)  # MOV W2, #1
expect(0xc59874, 0xd63f0100)  # BLR X8 (extension virtual +104)
call(0xc59c70, 0xc5a018)
expect(0xc59c98, 0x52800062)  # MOV W2, #3
expect(0xc59cb8, 0xd63f0100)

# Bloom draws from context's default colour target; Stasis binds that same target.
expect(0xc5a058, 0xf942a12b)  # LDR X11, [X9,#0x540]
expect(0xc5a08c, 0xf9401169)  # LDR X9, [X11,#0x20]
expect(0xc5a090, 0x91002129)  # ADD X9, X9,#8 (TextureData inside colour target)
expect(0xc5a094, 0xf9001fe9)  # STR X9, [SP,#56] (Bloom draw argument)
call(0xc5a0d8, 0xc5efa0)
receipt = {"binary_sha256": hashlib.sha256(data).hexdigest(), "phases": list(map(hex, phases)),
           "world_before_bloom": True, "hud_after_bloom_before_mask_release": True,
           "default_colour_is_bloom_source": True, "visual_fidelity_observed": False}
args.output.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
print(json.dumps(receipt))
