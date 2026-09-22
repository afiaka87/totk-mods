// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include "Vec3.hpp"

namespace arrowbound::pure {
// Adapted from Self Recall: inspect only after both native model lanes finish.
enum class ModelJoinStatus { Waiting, Complete, UnknownScene, WrongEpoch };
template<unsigned Capacity = 64> class ModelCompletionJoin {
    struct Slot { std::atomic<std::uintptr_t> queue{0}; std::atomic<unsigned> done{0}; };
    std::array<Slot, Capacity> slots_{};
    std::atomic<std::uint64_t> epoch_{};
public:
    void beginFrame(std::uint64_t epoch) {
        for (auto& slot : slots_) { slot.queue.store(0); slot.done.store(0); }
        epoch_.store(epoch, std::memory_order_release);
    }
    bool registerScene(std::uintptr_t queue, std::uint64_t epoch) {
        if (!queue || !epoch || epoch != epoch_.load(std::memory_order_acquire)) return false;
        for (auto& slot : slots_) {
            std::uintptr_t empty = 0;
            if (slot.queue.compare_exchange_strong(empty, queue) || empty == queue) return true;
        }
        return false;
    }
    ModelJoinStatus complete(std::uintptr_t queue, std::uint64_t epoch, unsigned lane) {
        if (!epoch || epoch != epoch_.load(std::memory_order_acquire)) return ModelJoinStatus::WrongEpoch;
        for (auto& slot : slots_) {
            if (!queue || slot.queue.load(std::memory_order_acquire) != queue) continue;
            const auto previous = slot.done.fetch_or(lane, std::memory_order_acq_rel);
            return previous != 3 && (previous | lane) == 3 ? ModelJoinStatus::Complete : ModelJoinStatus::Waiting;
        }
        return ModelJoinStatus::UnknownScene;
    }
};
template<class T> T modelRead(const void* p, std::size_t offset = 0) {
    T value;
    std::memcpy(&value, static_cast<const char*>(p) + offset, sizeof(value));
    return value;
}
struct ModelAdmission { std::uintptr_t unit{}; bool present{}, admitted{}; };

struct ModelCullSnapshot {
    std::uint32_t mask{}, renderFlags{};
    std::uint16_t shapes{};
    std::uint8_t viewCount{};
    bool grouped{}, spherePresent{}, sphereValid{};
    Vec3 center{};
    float radius{};
    const void* views{};
};
inline ModelCullSnapshot inspectModelCull(const void* unit) {
    ModelCullSnapshot out;
    out.mask = modelRead<std::uint32_t>(unit,0xC);
    out.shapes = modelRead<std::uint16_t>(unit,0x24);
    const auto* render = modelRead<const void*>(unit,0x30);
    if (render) {
        out.viewCount = modelRead<std::uint8_t>(render,8);
        out.renderFlags = modelRead<std::uint32_t>(render,0xC);
    }
    out.views = modelRead<const void*>(unit,0x38);
    const auto* group = modelRead<const char*>(unit,0x40);
    out.grouped = group != nullptr;
    const void* sphere = group ? group+0x8C : modelRead<const void*>(unit,0x58);
    if (sphere) {
        out.spherePresent = true;
        out.center = modelRead<Vec3>(sphere);
        out.radius = modelRead<float>(sphere,0xC);
        out.sphereValid = finite3(out.center) && std::isfinite(out.radius) && out.radius >= 0;
    }
    return out;
}
inline bool inspectModelQueue(const void* queue, std::span<ModelAdmission> models) {
    if (!queue) return false;
    const auto singlesCount = modelRead<std::uint32_t>(queue, 0x20);
    const auto groupsCount = modelRead<std::uint32_t>(queue, 0x58);
    if (singlesCount > 16384 || groupsCount > 16384) return false;
    const auto* singles = modelRead<const void* const*>(queue, 0x28);
    const auto* groups = modelRead<const void* const*>(queue, 0x60);
    if ((!singles && singlesCount) || (!groups && groupsCount)) return false;
    for (auto& model : models) { model.present = false; model.admitted = false; }
    unsigned visited = 0;
    const auto walk = [&](const void* node) {
        for (; node; node = modelRead<const void*>(node, 8)) {
            if (++visited > 65536) return false;
            const auto unit = modelRead<std::uintptr_t>(node);
            for (auto& model : models) if (unit == model.unit) {
                model.present = true;
                model.admitted |= (modelRead<std::uint8_t>(node, 0x1E) & 0x40) != 0;
            }
        }
        return true;
    };
    for (unsigned i = 0; i < singlesCount; ++i) {
        if (!singles[i]) return false;
        const auto* entries = modelRead<const void* const*>(singles[i], 0x28);
        if (!entries || !entries[0] || !walk(entries[0])) return false;
    }
    for (unsigned i = 0; i < groupsCount; ++i) if (!walk(groups[i])) return false;
    return true;
}
inline unsigned visibleBitCount(const std::uint32_t* bits, unsigned count) {
    unsigned visible = 0;
    for (unsigned i = 0; i < count; ++i) visible += (bits[i / 32] >> (i % 32)) & 1;
    return visible;
}
inline Vec3 modelBonePosition(const float bone[16], Vec3 origin, bool relative) {
    const Vec3 translation{bone[12], bone[13], bone[14]};
    return relative ? add(translation, origin) : translation;
}
}
