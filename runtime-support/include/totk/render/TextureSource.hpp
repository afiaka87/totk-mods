// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <lib.hpp>
#include <nn/fs.h>
#include <atomic>
#include <cstdint>
#include <span>
namespace totk::assets {
struct Slice { std::uint32_t offset, bytes, crc; };
inline constexpr char kBombTexturePath[] = "content:/BotwAbilities/Textures/Bomb.bin";
inline constexpr char kCryonisTexturePath[] = "content:/BotwAbilities/Textures/Cryonis.bin";
inline std::uint32_t crc32(std::span<const unsigned char> bytes) {
    auto crc = ~std::uint32_t{};
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
inline constexpr std::size_t kTextureScratchBytes = 320000;
alignas(16) inline unsigned char textureScratch[kTextureScratchBytes];
inline std::atomic_flag textureLock = ATOMIC_FLAG_INIT;
template <class Upload> bool withTexture(const char* path, Slice slice, Upload upload) {
    if (!slice.bytes || slice.bytes > sizeof(textureScratch) ||
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
        if (good) {
            result = nn::fs::ReadFile(file, slice.offset, textureScratch, slice.bytes);
            good = result.IsSuccess() && crc32({textureScratch, slice.bytes}) == slice.crc;
        }
        nn::fs::CloseFile(file);
    }
    if (good)
        upload(textureScratch);
    else
        Logging.Log("[texture] LOAD_FAILED path=%s offset=%u bytes=%u result=%08x",
                    path, slice.offset, slice.bytes, result.GetInnerValueForDebug());
    textureLock.clear(std::memory_order_release);
    return good;
}
}
