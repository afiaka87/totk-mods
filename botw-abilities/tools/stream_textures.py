# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Externalize exact generated texture bytes without changing geometry or texture metadata."""

import argparse
import hashlib
import json
import re
import zlib
from pathlib import Path

ARRAY = re.compile(
    r"(?:alignas\(\d+\)\s+)?inline constexpr unsigned char (\w+)\[\]\s*=\s*\{([^}]+)\};",
    re.S,
)


def externalize(source: str, blob: bytearray, records: list) -> str:
    current = []

    def replace(match):
        name = match[1]
        data = bytes(
            int(token.strip(), 0) for token in match[2].split(",") if token.strip()
        )
        assert 0 < len(data) <= 320000
        offset = len(blob)
        blob.extend(data)
        records.append(
            {
                "name": name,
                "offset": offset,
                "bytes": len(data),
                "crc": zlib.crc32(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
        current.append(records[-1])
        return f"inline constexpr totk::assets::Slice {name}{{{offset},{len(data)},{zlib.crc32(data)}}};"

    source = ARRAY.sub(replace, source)
    for record in current:
        source = re.sub(
            r"\bsizeof\(\s*" + re.escape(record["name"]) + r"\s*\)",
            str(record["bytes"]),
            source,
        )
    source = source.replace("const unsigned char *data;", "totk::assets::Slice data;")
    return source.replace(
        "#pragma once", '#pragma once\n#include "totk/render/TextureSource.hpp"'
    )


def build(repo: Path, output: Path):
    headers = output / "generated/streamed"
    assets = output / "streamed-assets/BotwAbilities/Textures"
    headers.mkdir(parents=True, exist_ok=True)
    assets.mkdir(parents=True, exist_ok=True)
    receipt = {}
    for group, member, names in (
        (
            "Bomb",
            "totk-remote-bomb",
            (
                "RemoteBombAssets.hpp",
                "RemoteBombHudAssets.hpp",
                "RemoteBombEffectAssets.hpp",
            ),
        ),
        ("Cryonis", "totk-cryonis", ("CryonisAssets.hpp",)),
    ):
        blob, records = bytearray(), []
        for name in names:
            source = repo / "mods/code" / member / "build/generated" / name
            transformed = externalize(source.read_text(), blob, records)
            destination = headers / name
            if not destination.exists() or destination.read_text() != transformed:
                destination.write_text(transformed, newline="\n")
        assert records, group
        path = assets / (group + ".bin")
        if not path.exists() or path.read_bytes() != blob:
            path.write_bytes(blob)
        receipt[group] = {
            "bytes": len(blob),
            "sha256": hashlib.sha256(blob).hexdigest(),
            "slices": records,
        }
    (headers / "textures.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("Exact streamed textures:", {k: v["bytes"] for k, v in receipt.items()})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build(args.repo, args.output)
