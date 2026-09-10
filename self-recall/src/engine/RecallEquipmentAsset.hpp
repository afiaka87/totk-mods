#pragma once
#include "RecallModelView.hpp"
#include "RecallResourceLease.hpp"
#include "RecallArchiveLifecycle.hpp"
#include <array>
#include <atomic>

namespace self_recall::equipment::detail {
constexpr unsigned kAssetLimit = 64;
constexpr unsigned kResourceLimit = pure::kPoseModelLimit;
using Life = pure::ArchiveLife;
struct Asset {
    std::atomic<Life> life{Life::Empty};
    std::atomic<const void*> root{nullptr};
    const void* sourceRoot = nullptr; // comparison only
    std::uint32_t actorId = 0, world = 0;
    std::uint16_t count = 0, resourceCount = 0;
    pure::PoseFrameKey last{};
    unsigned appearance = 0;
    std::array<model::Identity, pure::kPoseModelLimit> source{}, copy{};
    std::array<model::ResourceLease, kResourceLimit> resources;
};
} // namespace self_recall::equipment::detail
