# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Keep exact glyph pixels and advances for the abilities' fixed prompt strings."""

import argparse
import re
from pathlib import Path

SOURCE_FACES = 3
PROMPT_FACES = 2
GLYPHS_PER_FACE = 96
GLYPH_BYTES = 48 * 48
TEXTURE_BANK_BYTES = 36864
PROMPTS = ("Freeze", "Freezing", "Shatter", "Recharging", "B / Tap L  Cancel", "?")
PIXELS = re.compile(
    r"(?:alignas\(\d+\)\s+)?inline\s+(?:constexpr\s+)?unsigned char kPixels\[[^\]]*\]"
    r"\s*=\s*\{(?P<bytes>[^}]+)\};",
    re.S,
)


def pack_glyphs(original: bytes) -> tuple[bytes, list[list[int]]]:
    cells = sorted(
        {ord(char) - 32 for prompt in PROMPTS for char in prompt if char != " "}
    )
    atlas = bytearray()
    slots = []
    for face in range(PROMPT_FACES):
        mapping = {}
        for cell in cells:
            mapping[cell] = len(atlas) // GLYPH_BYTES
            start = (face * GLYPHS_PER_FACE + cell) * GLYPH_BYTES
            atlas.extend(original[start : start + GLYPH_BYTES])
        fallback = mapping[ord("?") - 32]
        slots.append([mapping.get(cell, fallback) for cell in range(GLYPHS_PER_FACE)])
    slots.append([slots[0][ord("?") - 32]] * GLYPHS_PER_FACE)
    atlas.extend(bytes(-len(atlas) % TEXTURE_BANK_BYTES))
    for face in range(PROMPT_FACES):
        for cell in cells:
            packed = slots[face][cell] * GLYPH_BYTES
            source = (face * GLYPHS_PER_FACE + cell) * GLYPH_BYTES
            assert (
                atlas[packed : packed + GLYPH_BYTES]
                == original[source : source + GLYPH_BYTES]
            )
    return bytes(atlas), slots


def compact_header(text: str) -> tuple[str, int, int]:
    match = PIXELS.search(text)
    if match is None:
        raise ValueError("font pixel array missing")
    original = bytes(
        int(value.strip(), 0) for value in match["bytes"].split(",") if value.strip()
    )
    if len(original) != SOURCE_FACES * GLYPHS_PER_FACE * GLYPH_BYTES:
        raise ValueError("unexpected source font dimensions")
    atlas, slots = pack_glyphs(original)
    pixels = ",\n".join(
        ",".join(map(str, atlas[start : start + 64]))
        for start in range(0, len(atlas), 64)
    )
    declaration = "alignas(4096) inline unsigned char kPixels[]={\n" + pixels + "\n};"
    text = text[: match.start()] + declaration + text[match.end() :]
    rows = ",\n".join("{" + ",".join(map(str, row)) + "}" for row in slots)
    mapping = "inline constexpr std::uint16_t kGlyphSlots[3][96]={\n" + rows + "\n};\n"
    text = text.replace(
        "inline native_font::Atlas atlas()",
        mapping + "inline native_font::Atlas atlas()",
    )
    text = text.replace(
        "kPixels,sizeof(kPixels),kAdvances",
        "kPixels,sizeof(kPixels),kAdvances,kGlyphSlots",
    )
    return text, len(original), len(atlas)


def build(source: Path, output: Path) -> None:
    text, original_bytes, compact_bytes = compact_header(source.read_text())
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text, newline="\n")
    print(
        f"Compact font: {original_bytes} -> {compact_bytes} bytes; exact pixels and advances retained"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.output)
