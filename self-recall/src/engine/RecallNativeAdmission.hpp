#pragma once

#include <array>
#include <span>
#include "RecallPoseHistory.hpp"

namespace self_recall::model {

enum class AdmissionStatus : std::uint8_t {
    Ready, InvalidRoster, WrongScene, ClosedQueue, MissingUnit, DetachedModel, QueueFull,
};

struct NativeAdmissionPlan {
    static constexpr unsigned kRootLimit = 20;
    AdmissionStatus status = AdmissionStatus::InvalidRoster;
    std::array<const void*, kRootLimit> request{};
    unsigned count = 0;
};

inline NativeAdmissionPlan planNativeAdmission(const void* scene, std::span<const void* const> roots,
        std::span<const pure::RecordedModelPose> models) {
    NativeAdmissionPlan plan;
    if (!scene || roots.empty() || roots.size() > NativeAdmissionPlan::kRootLimit ||
        models.empty() || models.size() > pure::kPoseModelLimit) return plan;
    const auto read = []<class T>(const void* p, std::size_t offset, T& out) {
        std::memcpy(&out, static_cast<const std::byte*>(p) + offset, sizeof(out));
    };
    std::uint8_t queueFlags;
    read(scene, 0x42A8, queueFlags);
    if (!(queueFlags & 1u)) { plan.status = AdmissionStatus::ClosedQueue; return plan; }
    std::array<bool, pure::kPoseModelLimit> found{};
    unsigned required[2]{};
    for (unsigned r = 0; r < roots.size(); ++r) {
        const auto* root = roots[r];
        if (!root) return plan;
        for (unsigned j = 0; j < r; ++j) if (roots[j] == root) return plan;
        const void* owner;
        read(root, 0x60, owner);
        if (owner != scene) { plan.status = AdmissionStatus::WrongScene; return plan; }
        int count;
        const void* const* entries;
        read(root, 0x20, count);
        read(root, 0x28, entries);
        if (count <= 0 || count > pure::kPoseModelLimit || !entries) return plan;
        bool draw = false;
        for (int i = 0; i < count; ++i) {
            if (!entries[i]) return plan;
            std::uintptr_t unit;
            read(entries[i], 0, unit);
            bool matched = false;
            for (unsigned m = 0; m < models.size(); ++m) {
                if (models[m].identity.unit != unit) continue;
                if (models[m].queueAdmission > 1) return plan;
                found[m] = matched = true;
                draw |= models[m].queueAdmission != 0;
                break;
            }
            if (!matched) { plan.status = AdmissionStatus::MissingUnit; return plan; }
        }
        if (!draw) continue;
        std::uint8_t flags, state;
        read(root, 0x240, flags);
        read(root, 0x241, state);
        if ((flags & 8u) || (state & 1u)) {
            plan.status = AdmissionStatus::DetachedModel;
            return plan;
        }
        if (!(state & 2u)) {
            plan.request[plan.count++] = root;
            ++required[count != 1];
        }
    }
    for (unsigned m = 0; m < models.size(); ++m)
        if (!found[m]) { plan.status = AdmissionStatus::MissingUnit; return plan; }
    for (unsigned lane = 0; lane < 2; ++lane) {
        int used, capacity;
        read(scene, 0x42C0 + lane * 0x10, used);
        read(scene, 0x42C4 + lane * 0x10, capacity);
        if (used < 0 || capacity < used || required[lane] > static_cast<unsigned>(capacity - used)) {
            plan.status = AdmissionStatus::QueueFull;
            return plan;
        }
    }
    plan.status = AdmissionStatus::Ready;
    return plan;
}

} // namespace self_recall::model
