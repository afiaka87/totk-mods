# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Build the abilities page's wheel icons from BotW's own Stasis+ and Cryonis inventory icons.

Each BotW icon (144x144 sRGB RGBA8) is encoded to ASTC 4x4 sRGB and written into a copy of the
TotK Camera wheel icon, whose bank has the same size, format and single mip. Only the texture
bytes and the texture name change.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ICONS = {"BotwStasis": "Obj_StopTimerLv2", "BotwCryonis": "Obj_IceMaker",
         "BotwBomb": "Obj_RemoteBombBall2", "BotwBombCube": "Obj_RemoteBombCube2"}
TEMPLATE = "UI/Tex/Icon/Obj_Camera.bntx.zs"
TEMPLATE_NAME = "Obj_Camera"
SIZE = 144
ASTC_SRGB = 0x2D06
RGBA8_SRGB = 0x0B06
BLOCK_BYTES = 16
# The bank's name pool entry for the texture: u16 length, text, NUL, padded to the next entry.
NAME_AT, NAME_END = 0x1B8, 0x1C8


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def botw_icon(romfs: Path, actor: str, oead, Bank) -> bytes:
    raw = bytes(oead.yaz0.decompress((romfs / f"UI/StockItem/{actor}.sbitemico").read_bytes()))
    start = raw.find(b"BNTX")
    if start < 0:
        raise ValueError(f"{actor}: no texture bank in the item icon")
    bank = Bank(raw[start:])
    if len(bank.textures) != 1:
        raise ValueError(f"{actor}: expected one texture, found {len(bank.textures)}")
    info = bank.textures[0]
    if (info.width, info.height, info.format_id, info.mip_count) != (SIZE, SIZE, RGBA8_SRGB, 1):
        raise ValueError(f"{actor}: unexpected source texture {info}")
    return bank.linear(info)


def encode(rgba: bytes, astc) -> tuple[bytes, bytes]:
    config = astc.ASTCConfig(astc.ASTCProfile.LDR_SRGB, 4, 4, 1, astc.ASTCQualityPreset.THOROUGH)
    context = astc.ASTCContext(config)
    swizzle = astc.ASTCSwizzle.from_str("RGBA")
    blocks = context.compress(astc.ASTCImage(astc.ASTCType.U8, SIZE, SIZE, 1, rgba), swizzle)
    decoded = astc.ASTCImage(astc.ASTCType.U8, SIZE, SIZE, 1)
    context.decompress(blocks, decoded, swizzle)
    return bytes(blocks), bytes(decoded.data)


def rename(bank: bytearray, name: str) -> None:
    old = bank[NAME_AT : NAME_END]
    length = struct.unpack_from("<H", old)[0]
    if old[2 : 2 + length].decode() != TEMPLATE_NAME:
        raise ValueError("the template's texture name is not where expected")
    text = name.encode()
    entry = struct.pack("<H", len(text)) + text + b"\0"
    if len(entry) > NAME_END - NAME_AT:
        raise ValueError(f"{name} does not fit the template's name entry")
    bank[NAME_AT:NAME_END] = entry.ljust(NAME_END - NAME_AT, b"\0")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--botw-romfs", type=Path, required=True, help="BotW 1.6.0 Switch RomFS")
    parser.add_argument("--totk-romfs", type=Path, required=True, help="clean TotK 1.2.1 RomFS")
    parser.add_argument("--output", type=Path, required=True, help="romfs tree to write")
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.repo / "scripts"))
    import astc_encoder as astc
    import oead
    from totk_mod_tools.textures import swizzle
    from totk_mod_tools.textures.bntx import Bank
    from totk_mod_tools.zstd_helper import TotkZstd

    z = TotkZstd(args.totk_romfs)
    template = z.decompress(args.totk_romfs / TEMPLATE)
    info = Bank(template).textures[0]
    if (info.name, info.width, info.height, info.format_id, info.mip_count) != (
        TEMPLATE_NAME, SIZE, SIZE, ASTC_SRGB, 1
    ):
        raise ValueError(f"unexpected template texture {info}")
    ew, eh = info.elements
    size = swizzle.swizzled_size(ew, eh, BLOCK_BYTES, info.block_height)
    surface = template[info.data_offset : info.data_offset + size]
    if bytes(swizzle.swizzle(Bank(template).linear(info), ew, eh, BLOCK_BYTES, info.block_height)) != surface:
        raise ValueError("the template does not round-trip through the swizzle")

    receipt = {"template": TEMPLATE, "template_sha256": sha256(template), "icons": {}}
    for name, actor in ICONS.items():
        rgba = botw_icon(args.botw_romfs, actor, oead, Bank)
        blocks, decoded = encode(rgba, astc)
        if len(blocks) != ew * eh * BLOCK_BYTES:
            raise ValueError(f"{name}: encoder wrote {len(blocks)} bytes")
        error = max(abs(a - b) for a, b in zip(rgba, decoded, strict=True))
        mean = sum(abs(a - b) for a, b in zip(rgba, decoded, strict=True)) / len(rgba)
        bank = bytearray(template)
        bank[info.data_offset : info.data_offset + size] = swizzle.swizzle(
            blocks, ew, eh, BLOCK_BYTES, info.block_height)
        rename(bank, name)
        out = Bank(bytes(bank)).textures[0]
        if (out.name, out.width, out.height, out.format_id, out.mip_count) != (
            name, SIZE, SIZE, ASTC_SRGB, 1
        ):
            raise ValueError(f"{name}: wrote {out}")
        if Bank(bytes(bank)).linear(out) != blocks or TEMPLATE_NAME.encode() in bank:
            raise ValueError(f"{name}: the written bank does not hold the new texture alone")
        target = args.output / f"UI/Tex/Icon/{name}.bntx.zs"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(z.compress(bytes(bank), target.name))
        if z.decompress(target) != bank:
            raise ValueError(f"{name}: the compressed icon does not round-trip")
        receipt["icons"][name] = {
            "source": f"UI/StockItem/{actor}.sbitemico", "source_rgba_sha256": sha256(rgba),
            "bntx_bytes": len(bank), "bntx_sha256": sha256(bytes(bank)),
            "file_bytes": target.stat().st_size, "file_sha256": sha256(target.read_bytes()),
            "astc_max_channel_error": error, "astc_mean_channel_error": round(mean, 3),
        }
        print(f"[botw-abilities] {name}: {target.stat().st_size} bytes, ASTC error max {error} "
              f"mean {mean:.2f}")
    args.receipt.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
