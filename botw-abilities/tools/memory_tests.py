# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Validate lossless sidecars and generated headers against the original local assets."""

import hashlib
import json
import re
import unittest
import zlib
from pathlib import Path

from stream_textures import ARRAY, externalize
from compact_font import (
    GLYPH_BYTES,
    GLYPHS_PER_FACE,
    PROMPTS,
    TEXTURE_BANK_BYTES,
    pack_glyphs,
)

ROOT = Path(__file__).resolve().parents[4]
BUILD = ROOT / "mods/code/botw-abilities/build"


class MemoryTests(unittest.TestCase):
    def test_compact_font_keeps_each_face_and_fallback(self):
        original = b"".join(
            bytes([glyph % 256]) * GLYPH_BYTES for glyph in range(3 * GLYPHS_PER_FACE)
        )
        atlas, slots = pack_glyphs(original)
        supported = {
            ord(char) - 32 for prompt in PROMPTS for char in prompt if char != " "
        }
        for face in range(2):
            for cell in range(GLYPHS_PER_FACE):
                source_cell = cell if cell in supported else ord("?") - 32
                start = slots[face][cell] * GLYPH_BYTES
                self.assertEqual(
                    atlas[start : start + GLYPH_BYTES],
                    bytes([face * GLYPHS_PER_FACE + source_cell]) * GLYPH_BYTES,
                )
        self.assertEqual(slots[2], [slots[0][ord("?") - 32]] * GLYPHS_PER_FACE)
        self.assertEqual(len(atlas) % TEXTURE_BANK_BYTES, 0)
        self.assertEqual(set(atlas[2 * len(supported) * GLYPH_BYTES :]), {0})

    def test_all_slices_match_original_bytes(self):
        receipt = json.loads((BUILD / "generated/streamed/textures.json").read_text())
        for group, member, names, count in (
            (
                "Bomb",
                "totk-remote-bomb",
                (
                    "RemoteBombAssets.hpp",
                    "RemoteBombHudAssets.hpp",
                    "RemoteBombEffectAssets.hpp",
                ),
                36,
            ),
            ("Cryonis", "totk-cryonis", ("CryonisAssets.hpp",), 3),
        ):
            blob = (
                BUILD / f"streamed-assets/BotwAbilities/Textures/{group}.bin"
            ).read_bytes()
            expected = []
            for name in names:
                source = (
                    ROOT / "mods/code" / member / "build/generated" / name
                ).read_text()
                expected.extend(
                    bytes(int(v.strip(), 0) for v in match[2].split(",") if v.strip())
                    for match in ARRAY.finditer(source)
                )
            self.assertEqual(len(expected), count)
            self.assertEqual(len(blob), receipt[group]["bytes"])
            self.assertEqual(hashlib.sha256(blob).hexdigest(), receipt[group]["sha256"])
            cursor = 0
            for image, record in zip(expected, receipt[group]["slices"], strict=True):
                self.assertEqual(record["offset"], cursor)
                actual = blob[cursor : cursor + record["bytes"]]
                self.assertEqual(image, actual)
                self.assertEqual(record["crc"], zlib.crc32(actual))
                self.assertEqual(record["sha256"], hashlib.sha256(actual).hexdigest())
                cursor += record["bytes"]
            self.assertEqual(cursor, len(blob))

    def test_names_in_different_namespaces_keep_their_own_sizes(self):
        blob, records = bytearray(), []
        for size in (7, 3):
            source = (
                "inline constexpr unsigned char image0[] = {"
                + ",".join(["2"] * size)
                + "};\nstatic_assert(sizeof(image0)>0);"
            )
            result = externalize(source, blob, records)
            self.assertIn(f"static_assert({size}>0)", result)

    def test_texture_metadata_and_geometry_are_unchanged(self):
        for member, names in (
            (
                "totk-remote-bomb",
                (
                    "RemoteBombAssets.hpp",
                    "RemoteBombHudAssets.hpp",
                    "RemoteBombEffectAssets.hpp",
                ),
            ),
            ("totk-cryonis", ("CryonisAssets.hpp",)),
        ):
            for name in names:
                original = (
                    ROOT / "mods/code" / member / "build/generated" / name
                ).read_text()
                transformed = (BUILD / "generated/streamed" / name).read_text()
                stripped = ARRAY.sub("", original).replace(
                    "const unsigned char *data;", "totk::assets::Slice data;"
                )
                for match in ARRAY.finditer(original):
                    size = len([v for v in match[2].split(",") if v.strip()])
                    stripped = re.sub(
                        r"\bsizeof\(\s*" + re.escape(match[1]) + r"\s*\)",
                        str(size),
                        stripped,
                    )
                stripped = stripped.replace(
                    "#pragma once",
                    '#pragma once\n#include "totk/render/TextureSource.hpp"',
                )
                actual = re.sub(
                    r"inline constexpr totk::assets::Slice \w+\{\d+,\d+,\d+\};",
                    "",
                    transformed,
                )
                self.assertEqual(actual, stripped, name)


if __name__ == "__main__":
    unittest.main()
