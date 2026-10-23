// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "GalePoseStorage.hpp"
#include "GalePoseInfo.hpp"
#include <atomic>
#include <lib.hpp>
#include <nn/fs.h>

namespace botw_abilities::champions::pose {
namespace {
alignas(16) std::array<std::byte, kGalePoseBytes> g_bytes;
std::optional<GalePose> g_pose;
std::atomic<unsigned> g_state{};
constexpr char kPath[] = "content:/BotwAbilities/Champions/GalePose.bin";
} // namespace

const GalePose* asset() {
    return g_state.load(std::memory_order_acquire) == 2 ? &*g_pose : nullptr;
}

bool load() {
    if (asset()) return true;
    unsigned expected = 0;
    if (!g_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) return false;
    nn::fs::FileHandle file{};
    auto result = nn::fs::OpenFile(&file, kPath, nn::fs::OpenMode_Read);
    bool good = result.IsSuccess();
    s64 size = 0;
    if (good) {
        result = nn::fs::GetFileSize(&size, file);
        good = result.IsSuccess() && size == s64(g_bytes.size());
        if (good) {
            result = nn::fs::ReadFile(file, 0, g_bytes.data(), g_bytes.size());
            good = result.IsSuccess();
        }
        nn::fs::CloseFile(file);
    }
    if (good) {
        const auto parsed = GalePose::parse(g_bytes);
        good = parsed && parsed->fingerprint() == kGalePoseChecksum;
        if (good) g_pose = parsed;
    }
    g_state.store(good ? 2 : 0, std::memory_order_release);
    if (!good)
        Logging.Log("[champions] GALE_POSE_LOAD_REFUSED result=%08x bytes=%lld expected=%u",
            result.GetInnerValueForDebug(), static_cast<long long>(size), unsigned(g_bytes.size()));
    else
        Logging.Log("[champions] GALE_POSE_LOADED bytes=%u bones=%u checksum=%08x loop=%u",
            unsigned(g_bytes.size()), g_pose->boneCount(), kGalePoseChecksum, g_pose->continuation());
    return good;
}
} // namespace botw_abilities::champions::pose
