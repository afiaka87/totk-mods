// Copyright (c) HanSoloBlaster, afiaka87
// Reads the unpacked size a zstd file declares in its first bytes.
#pragma once

#include "Work.hpp"

namespace fieldworks::merge {

constexpr std::size_t kFrameHeadBytes = 18;

// False when the bytes are not a zstd frame or the frame does not declare its size.
inline bool frameContentSize(const u8* p, std::size_t bytes, u64& out) {
    if (bytes < 6 || rd32(p, false) != 0xfd2fb528u) return false;
    const u8 descriptor = p[4];
    const bool single = descriptor & 0x20;
    const int sizeFlag = descriptor >> 6, dictionaryFlag = descriptor & 3;
    std::size_t at = 5 + (single ? 0 : 1) + (dictionaryFlag == 3 ? 4 : dictionaryFlag);
    const std::size_t width = sizeFlag == 0 ? (single ? 1 : 0) : std::size_t{1} << sizeFlag;
    if (!width || at + width > bytes) return false;
    out = 0;
    for (std::size_t i = 0; i < width; ++i) out |= u64{p[at + i]} << (8 * i);
    if (width == 2) out += 256;
    return true;
}

}  // namespace fieldworks::merge
