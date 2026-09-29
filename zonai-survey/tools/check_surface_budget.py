# SPDX-License-Identifier: MIT
"""Fail closed on module growth or accidentally linked unused data or font code."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess

NATIVE_FONT_SYMBOLS = ("TextWriter", "DebugFont", "BootupInitDebugDrawersHook", "DebugDrawHook", "openSurveyAsset")

def nso_profile(data):
    if len(data) < 0x100 or data[:4] != b"NSO0":
        raise ValueError("Expected an NSO0 header")
    segments = {}
    for name, offset in (("text", 0x10), ("rodata", 0x20), ("data", 0x30)):
        _, address, size = struct.unpack_from("<III", data, offset)
        if address % 4096:
            raise ValueError("Unexpected unaligned NSO segment")
        segments[name] = {"offset": address, "bytes": size}
    bss = struct.unpack_from("<I", data, 0x3C)[0]
    data_end = segments["data"]["offset"] + segments["data"]["bytes"] + bss
    end = max(data_end, *(s["offset"] + s["bytes"] for s in segments.values()))
    return {"sha256": hashlib.sha256(data).hexdigest(), "file_bytes": len(data),
            "segments": segments, "bss_bytes": bss, "mapped_bytes": (end + 4095) & ~4095}

def validate(profile, symbols, compact=True):
    if profile["mapped_bytes"] > 1024*1024:
        raise ValueError("Depth Survey exceeded its 1 MiB mapped-module budget")
    if compact and any(line.endswith(" zonai_survey::glyphs::kPlacements") for line in symbols.splitlines()):
        raise ValueError("Unused data linked into compact Survey: zonai_survey::glyphs::kPlacements")
    for name in NATIVE_FONT_SYMBOLS:
        if name in symbols:
            raise ValueError(f"Native font dependency linked into atlas Survey: {name}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--nso", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--nm", type=Path, required=True)
    parser.add_argument("--compact", action="store_true")
    args = parser.parse_args()
    result = subprocess.run([str(args.nm), "--defined-only", "--demangle", str(args.elf)],
                            capture_output=True, text=True, check=True, timeout=30)
    profile = nso_profile(args.nso.read_bytes())
    validate(profile, result.stdout, args.compact)
    print(f"Survey budget OK: mapped={profile['mapped_bytes']} ceiling=1048576; not total runtime RAM")
