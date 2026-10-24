// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <lib.hpp>
#include <nn/fs.h>
#include <atomic>
#include <cstdint>
#include <span>
#include <algorithm>
namespace totk::assets {
struct Slice { std::uint32_t offset, bytes, crc; };
inline constexpr char kBombTexturePath[] = "content:/BotwAbilities/Textures/Bomb.bin";
inline constexpr char kCryonisTexturePath[] = "content:/BotwAbilities/Textures/Cryonis.bin";
inline std::uint32_t crc32Update(std::uint32_t crc, std::span<const unsigned char> bytes) {
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}
inline std::uint32_t crc32(std::span<const unsigned char> bytes) {
    return ~crc32Update(~std::uint32_t{},bytes);
}
inline constexpr std::size_t kTextureScratchBytes = 8192;
inline constexpr unsigned kMaximumTextureBytes = 320000;
alignas(16) inline unsigned char textureScratch[kTextureScratchBytes];
inline std::atomic_flag textureLock = ATOMIC_FLAG_INIT;
struct TextureRows { unsigned bytes{}, height{}; };
inline TextureRows textureRows(unsigned width,unsigned format) {
    unsigned block=1,bytes{};
    switch (format) {
    case 0x0D: bytes=2;break; // RG8
    case 0x25: bytes=4;break; // RGBA8
    case 0x42: case 0x49: block=4;bytes=8;break; // DXT1, RGTC1
    case 0x4B: block=4;bytes=16;break; // RGTC2
    default: return {};
    }
    const auto rowBytes=((std::uint64_t(width)+block-1)/block)*bytes;
    if (!width || rowBytes>kTextureScratchBytes) return {};
    return {unsigned(rowBytes),block};
}
template <class Upload> bool withTexture(const char* path, Slice slice, unsigned width,
                                        unsigned height,unsigned format,Upload upload) {
    const auto row=textureRows(width,format);
    if (!row.bytes || !height || !slice.bytes || slice.bytes>kMaximumTextureBytes ||
        ((std::uint64_t(height)+row.height-1)/row.height)*row.bytes!=slice.bytes ||
        textureLock.test_and_set(std::memory_order_acquire)) {
        Logging.Log("[texture] LOAD_REFUSED bytes=%u busy_or_capacity=1", slice.bytes);
        return false;
    }
    nn::fs::FileHandle file{};
    auto result = nn::fs::OpenFile(&file, path, nn::fs::OpenMode_Read);
    bool good = result.IsSuccess();
    if (good) {
        s64 size{};
        result = nn::fs::GetFileSize(&size, file);
        good = result.IsSuccess() && size >= 0 &&
               std::uint64_t(slice.offset) + slice.bytes <= std::uint64_t(size);
        // Validate the complete source before any GPU writes; verify again while uploading.
        for (unsigned pass=0;good && pass<2;++pass) {
            auto crc=~std::uint32_t{};
            const auto chunk=pass ? (kTextureScratchBytes/row.bytes)*row.bytes : kTextureScratchBytes;
            for (unsigned offset=0;good && offset<slice.bytes;) {
                const auto bytes=std::min<unsigned>(unsigned(chunk),slice.bytes-offset);
                result=nn::fs::ReadFile(file,std::uint64_t(slice.offset)+offset,textureScratch,bytes);
                good=result.IsSuccess();
                if (!good) break;
                crc=crc32Update(crc,{textureScratch,bytes});
                if (pass) {
                    const auto y=(offset/row.bytes)*row.height;
                    const auto rows=std::min((bytes/row.bytes)*row.height,height-y);
                    upload(textureScratch,y,rows);
                }
                offset+=bytes;
            }
            good=good && ~crc==slice.crc;
        }
        nn::fs::CloseFile(file);
    }
    if (!good)
        Logging.Log("[texture] LOAD_FAILED path=%s offset=%u bytes=%u result=%08x",
                    path, slice.offset, slice.bytes, result.GetInnerValueForDebug());
    textureLock.clear(std::memory_order_release);
    return good;
}
}
