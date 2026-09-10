#pragma once

#include "RecallPoseHistory.hpp"
#include "RecallGameTime.hpp"
#include "RecallVisual.hpp"

namespace self_recall::pure {

struct NativePathRoute {
    PoseFrameKey anchor{};
    std::uint32_t world = 0, historyCount = 0;
    std::uint64_t oldestNanoseconds = 0, newestNanoseconds = 0;
    RenderRoute route{};
    float phases[kMaxRenderPoints]{};
};
struct NativePathPoint { Vec3 position{}; float phase = 0; };
struct NativePathFrame {
    PoseFrameKey key{};
    std::uint64_t epoch = 0;
    std::uint16_t count = 0;
    NativePathPoint points[kMaxRenderPoints + 1]{};
};
static_assert(kMaxRenderPoints + 1 < 512); // Native selected-path node pool.

enum class NativePathStatus : unsigned {
    Ready, Empty, WrongGeneration, WrongWorld, InvalidRange, InvalidPoint, InvalidRoute
};

inline Vec3 recallTrailPosition(const PoseFrameHeader& frame) {
    const auto& root = frame.route.pose.position;
    Vec3 position{root.x, root.y, root.z};
    if (frame.haveWaterHeight && std::isfinite(frame.waterHeight) &&
        root.y <= frame.waterHeight && frame.waterHeight - root.y <= 2.5f)
        position.y = frame.waterHeight + 0.03f;
    return position;
}

inline NativePathStatus buildNativePathFrame(NativePathFrame& out,
        const NativePathRoute& source, const PoseFrameHeader& selected, std::uint64_t epoch) {
    out.count = 0;
    out.key = {};
    out.epoch = 0;
    if (!source.anchor || !selected.key || source.anchor.generation != selected.key.generation)
        return NativePathStatus::WrongGeneration;
    if (!source.world || source.world != selected.worldGeneration) return NativePathStatus::WrongWorld;
    if (!epoch || source.historyCount < 2 || source.historyCount > kHistoryCapacity ||
        source.anchor.serial < selected.key.serial ||
        source.anchor.serial - selected.key.serial >= source.historyCount)
        return NativePathStatus::InvalidRange;
    if (source.newestNanoseconds <= source.oldestNanoseconds ||
        source.newestNanoseconds - source.oldestNanoseconds > kRecallWindowNanoseconds ||
        selected.elapsedNanoseconds < source.oldestNanoseconds ||
        selected.elapsedNanoseconds > source.newestNanoseconds) return NativePathStatus::InvalidRange;
    if (source.route.count > kMaxRenderPoints || !(selected.route.flags & SampleAdmissible))
        return NativePathStatus::InvalidRoute;
    const auto finite = [](Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
    const Vec3 root = recallTrailPosition(selected);
    if (!finite(root)) return NativePathStatus::InvalidPoint;
    const auto selectedIndex = source.anchor.serial - selected.key.serial;
    const auto remaining = source.historyCount - 1 - selectedIndex;
    unsigned previous = source.historyCount;
    float previousPhase = -1.0f;
    for (unsigned i = 0; i < source.route.count; ++i) {
        const auto& point = source.route.points[i];
        if (point.ordinal >= previous) return NativePathStatus::InvalidRoute;
        if (!finite(point.position)) return NativePathStatus::InvalidPoint;
        if (!std::isfinite(source.phases[i]) || source.phases[i] < 0 || source.phases[i] > 1 ||
            source.phases[i] < previousPhase) return NativePathStatus::InvalidRoute;
        previous = point.ordinal;
        previousPhase = source.phases[i];
    }
    const double duration = static_cast<double>(source.newestNanoseconds - source.oldestNanoseconds);
    out.points[0] = {root, static_cast<float>(
        static_cast<double>(source.newestNanoseconds - selected.elapsedNanoseconds) / duration)};
    unsigned count = 1;
    for (unsigned i = 0; i < source.route.count; ++i) {
        const auto& point = source.route.points[i];
        if (point.ordinal >= remaining) continue;
        if (source.phases[i] < out.points[0].phase) return NativePathStatus::InvalidRoute;
        const auto& last = out.points[count - 1].position;
        if (point.position.x == last.x && point.position.y == last.y && point.position.z == last.z) continue;
        out.points[count++] = {point.position, source.phases[i]};
    }
    out.key = selected.key;
    out.epoch = epoch;
    out.count = static_cast<std::uint16_t>(count);
    return count >= 2 ? NativePathStatus::Ready : NativePathStatus::Empty;
}

} // namespace self_recall::pure
