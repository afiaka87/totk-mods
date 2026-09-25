// SPDX-License-Identifier: MIT
#pragma once
#include "GlyphTables.hpp"
namespace zonai_survey::glyphs {
static_assert(kGridWidth==40 && kGridHeight==32 && kGridOriginCellX==-20 && kGridOriginCellZ==-16);
static_assert(kPosScale==0.25f && kCellSize==256.0f && kNameCount<=4096);
inline constexpr std::uint32_t kPackedPlacementBytes=(kPlacementCount*50+7)/8;
extern const std::uint8_t kPlacementBytes[kPackedPlacementBytes];
inline Placement unpackPlacement(std::uint32_t index, std::uint32_t cell) {
    if (index>=kPlacementCount || cell>=kGridWidth*kGridHeight) return {};
    std::uint64_t word=0;
    const auto bit=index*50;
    for (unsigned byte=0;byte<7;++byte) word|=std::uint64_t(kPlacementBytes[bit/8+byte])<<(byte*8);
    word>>=bit%8;
    const int x=int(word&1023)+(int(cell%kGridWidth)+kGridOriginCellX)*1024;
    const int z=int((word>>10)&1023)+(int(cell/kGridWidth)+kGridOriginCellZ)*1024;
    const int rawY=int((word>>20)&65535), y=rawY>=32768 ? rawY-65536 : rawY;
    return {std::int16_t(x),std::int16_t(y),std::int16_t(z),std::uint16_t((word>>36)&4095),std::uint16_t((word>>48)&3)};
}
}
