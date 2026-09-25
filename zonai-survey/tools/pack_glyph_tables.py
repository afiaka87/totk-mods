# SPDX-License-Identifier: MIT
"""Losslessly pack the existing quarter-metre collectible records, cell by cell."""
import argparse
import hashlib
import json
from pathlib import Path
import re

def pack(source: Path, output: Path):
    text = source.read_text(encoding="utf-8")
    block = re.search(r"const Placement kPlacements\[kPlacementCount\] = \{(.*?)\};", text, re.S)
    cells = re.search(r"const uint32_t kCellStart\[.*?\] = \{(.*?)\};", text, re.S)
    if not block or not cells:
        raise ValueError("Original table declarations changed")
    rows = [tuple(map(int, m)) for m in re.findall(r"\{(-?\d+),(-?\d+),(-?\d+),(\d+),(\d+)\}", block[1])]
    starts = list(map(int, re.findall(r"\d+", cells[1])))
    if len(rows) != 38701 or len(starts) != 1281 or starts[0] != 0 or starts[-1] != len(rows):
        raise ValueError("Original table counts changed; review cell format")
    data = bytearray()
    pending, pending_bits = 0, 0
    for cell, (begin, end) in enumerate(zip(starts, starts[1:])):
        if not 0 <= begin <= end <= len(rows):
            raise ValueError("Invalid cell range")
        origin_x, origin_z = (cell % 40 - 20) * 1024, (cell // 40 - 16) * 1024
        for x, y, z, name, flags in rows[begin:end]:
            lx, lz = x - origin_x, z - origin_z
            if not (0 <= lx < 1024 and 0 <= lz < 1024 and -32768 <= y <= 32767 and 0 <= name < 2053 and 0 <= flags < 4):
                raise ValueError(f"Record outside lossless encoding at {cell}: {(x,y,z,name,flags)}")
            word = lx | lz << 10 | (y & 65535) << 20 | name << 36 | flags << 48
            pending |= word << pending_bits
            pending_bits += 50
            while pending_bits >= 8:
                data.append(pending & 255)
                pending >>= 8
                pending_bits -= 8
    if pending_bits:
        data.append(pending)
    if len(data) != (len(rows)*50+7)//8:
        raise ValueError("Incomplete encoding")
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = ["// Generated losslessly by pack_glyph_tables.py; no record omitted.",
             '#include "PackedGlyphs.hpp"', "namespace zonai_survey::glyphs {",
             "const std::uint8_t kPlacementBytes[kPackedPlacementBytes] = {"]
    lines += [",".join(map(str, data[i:i+28])) + "," for i in range(0, len(data), 28)]
    lines += ["};", "}"]
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    receipt = {"source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
               "records": len(rows), "original_bytes": len(rows)*10, "packed_bytes": len(data),
               "packed_sha256": hashlib.sha256(data).hexdigest()}
    output.with_suffix(".json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(receipt))

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    pack(args.source, args.output)
