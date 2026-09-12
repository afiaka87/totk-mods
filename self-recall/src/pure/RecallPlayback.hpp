#pragma once

#include <cmath>
#include <cstring>

#include "RecallPoseData.hpp"

namespace self_recall::pure {

inline bool sameRenderSpace(const RecordedModelPose& a, const RecordedModelPose& b) {
    if (a.originRelative != b.originRelative || a.originRelative > 1) return false;
    return !a.originRelative || std::memcmp(a.renderOrigin, b.renderOrigin, sizeof(a.renderOrigin)) == 0;
}

inline bool boneToWorldMatrix(const RecordedBoneMatrix& bone,
                              const RecordedModelPose& model, float out[12]) {
    float source[16];
    std::memcpy(source, bone.words, sizeof(source));
    float result[12];
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 4; ++column) {
            const auto index = row * 4 + column;
            result[index] = source[column * 4 + row];
            if (column == 3 && model.originRelative)
                result[index] += model.renderOrigin[row];
            if (!std::isfinite(result[index])) return false;
        }
    }
    std::memcpy(out, result, sizeof(result));
    return true;
}

inline bool rebaseBoneForRender(const RecordedBoneMatrix& recorded,
                                const RecordedModelPose& from,
                                const RecordedModelPose& to,
                                RecordedBoneMatrix& out) {
    RecordedBoneMatrix result = recorded;
    for (unsigned axis = 0; axis < 3; ++axis) {
        float value;
        std::memcpy(&value, &recorded.words[12 + axis], sizeof(value));
        const double fromOrigin = from.originRelative ? from.renderOrigin[axis] : 0.0;
        const double toOrigin = to.originRelative ? to.renderOrigin[axis] : 0.0;
        const double delta = fromOrigin - toOrigin;
        if (delta != 0.0) value = static_cast<float>(static_cast<double>(value) + delta);
        if (!std::isfinite(value)) return false;
        std::memcpy(&result.words[12 + axis], &value, sizeof(value));
    }
    out = result;
    return true;
}

}

namespace self_recall::pure {

struct PosePresentation {
    PoseFrameKey key{};
    totk::core::WorldPosition offset{};
    explicit operator bool() const { return bool(key); }
};

inline bool finiteOffset(const totk::core::WorldPosition& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline void shiftPosition(totk::core::WorldPosition& p, const totk::core::WorldPosition& d) {
    p.x += d.x; p.y += d.y; p.z += d.z;
}
inline void shiftMatrix(float matrix[12], const totk::core::WorldPosition& d) {
    matrix[3] += d.x; matrix[7] += d.y; matrix[11] += d.z;
}
inline bool interpolatePosition(const PoseFrameHeader& newer, const PoseFrameHeader& older,
                                std::uint64_t target, PosePresentation& out) {
    if (older.elapsedNanoseconds >= newer.elapsedNanoseconds ||
        target < older.elapsedNanoseconds || target > newer.elapsedNanoseconds ||
        older.worldGeneration != newer.worldGeneration ||
        !(older.route.flags & SampleAdmissible) || !(newer.route.flags & SampleAdmissible) ||
        !finitePose(newer.route.pose) || !finitePose(older.route.pose)) return false;
    const double amount = double(newer.elapsedNanoseconds - target) /
                          double(newer.elapsedNanoseconds - older.elapsedNanoseconds);
    const auto& a = newer.route.pose.position;
    const auto& b = older.route.pose.position;
    out = {newer.key, {float((double(b.x) - a.x) * amount),
                      float((double(b.y) - a.y) * amount), float((double(b.z) - a.z) * amount)}};
    return finiteOffset(out.offset);
}

inline bool translateAnimation(RecordedPoseFrame& frame, const totk::core::WorldPosition& d) {
    if (!finiteOffset(d) || frame.header.boneCount > kPoseBoneLimit) return false;
    if (d.x == 0 && d.y == 0 && d.z == 0) return true;
    const float offset[3]{d.x, d.y, d.z};
    for (unsigned bone = 0; bone < frame.header.boneCount; ++bone) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            float value;
            std::memcpy(&value, &frame.bones[bone].words[12 + axis], sizeof(value));
            value += offset[axis];
            if (!std::isfinite(value)) return false;
            std::memcpy(&frame.bones[bone].words[12 + axis], &value, sizeof(value));
        }
    }
    shiftPosition(frame.header.route.pose.position, d);
    if (frame.header.haveWrist) shiftMatrix(frame.header.wristMatrix, d);
    return finitePose(frame.header.route.pose);
}

inline RecordedModelPose translatedBoundsSpace(RecordedModelPose source,
                                               const totk::core::WorldPosition& d) {
    const float offset[3]{d.x, d.y, d.z};
    for (unsigned axis = 0; axis < 3; ++axis)
        source.renderOrigin[axis] = (source.originRelative ? source.renderOrigin[axis] : 0) + offset[axis];
    source.originRelative = 1;
    return source;
}
}

#include "RecallBase.hpp"
namespace self_recall::pure {

enum class PosePlaybackStatus : std::uint8_t {
    Ready, Held, AtEnd, NoHistory, TooShort, InvalidClock,
    UnavailableFrame, WrongWorld, UnsafeSample, InvalidSpeed,
};

class PosePlayback {
public:
    PosePlaybackStatus begin(const PoseHistory& history, std::uint32_t world,
                             const GameTimeSnapshot& clock) {
        reset();
        if (clock.status != GameTimeStatus::Running) return PosePlaybackStatus::InvalidClock;
        auto latest = history.newest();
        const auto count = history.count();
        if (!latest || !count) return PosePlaybackStatus::NoHistory;
        const auto& newest = latest.get()->header;
        if (!world || newest.worldGeneration != world) return PosePlaybackStatus::WrongWorld;
        if (!(newest.route.flags & SampleAdmissible)) return PosePlaybackStatus::UnsafeSample;
        auto oldest = history.before(newest.key, count - 1);
        if (!oldest) return PosePlaybackStatus::UnavailableFrame;
        const auto firstTime = oldest.get()->header.elapsedNanoseconds;
        if (newest.elapsedNanoseconds < firstTime ||
            newest.elapsedNanoseconds > clock.elapsedNanoseconds)
            return PosePlaybackStatus::InvalidClock;
        const auto duration = newest.elapsedNanoseconds - firstTime;
        if (duration < kRecallMinimumNanoseconds) return PosePlaybackStatus::TooShort;
        if (duration > kRecallWindowNanoseconds) return PosePlaybackStatus::UnavailableFrame;
        history_ = &history;
        anchor_ = newest.key;
        count_ = count;
        world_ = world;
        newestTime_ = newest.elapsedNanoseconds;
        duration_ = duration;
        lastClock_ = clock.elapsedNanoseconds;
        selected_ = std::move(latest);
        presentation_ = {newest.key, {}};
        return PosePlaybackStatus::Ready;
    }

    PosePlaybackStatus step(const GameTimeSnapshot& clock, std::uint32_t world,
                            std::uint32_t allowedThrough = UINT32_MAX,
                            PlaybackRate rate = PlaybackRate::Normal) {
        if (!history_ || !selected_) return PosePlaybackStatus::NoHistory;
        if (world != world_) return PosePlaybackStatus::WrongWorld;
        if (history_->generation() != anchor_.generation) return PosePlaybackStatus::UnavailableFrame;
        if (!validPlaybackRate(rate)) return PosePlaybackStatus::InvalidSpeed;
        if (clock.elapsedNanoseconds < lastClock_) return PosePlaybackStatus::InvalidClock;
        if (clock.status == GameTimeStatus::Paused || clock.status == GameTimeStatus::NoAdvance) {
            lastClock_ = clock.elapsedNanoseconds;
            return PosePlaybackStatus::Held;
        }
        if (clock.status != GameTimeStatus::Running) return PosePlaybackStatus::InvalidClock;
        const auto delta = clock.elapsedNanoseconds - lastClock_;
        lastClock_ = clock.elapsedNanoseconds;
        const auto remaining = duration_ - elapsed_;
        const auto bounded = delta < remaining ? delta : remaining;
        const auto scaled = bounded * static_cast<unsigned>(rate) + rateRemainder_;
        const auto advance = scaled / 4;
        rateRemainder_ = static_cast<std::uint8_t>(scaled % 4);
        elapsed_ = advance >= remaining ? duration_ : elapsed_ + advance;
        if (allowedThrough < count_ - 1) {
            if (allowedThrough < index_) return PosePlaybackStatus::UnavailableFrame;
            PoseFrameHeader boundary;
            if (!headerAt(allowedThrough, boundary)) return PosePlaybackStatus::UnavailableFrame;
            const auto limit = newestTime_ - boundary.elapsedNanoseconds;
            if (elapsed_ >= limit) { elapsed_ = limit; rateRemainder_ = 0; }
        }
        const auto target = newestTime_ - elapsed_;
        std::uint32_t low = index_;
        std::uint32_t high = count_ - 1;
        while (low < high) {
            const auto middle = low + (high - low + 1) / 2;
            PoseFrameHeader frame;
            if (!headerAt(middle, frame)) return PosePlaybackStatus::UnavailableFrame;
            const auto recordedTime = frame.elapsedNanoseconds;
            const auto tolerance = (static_cast<unsigned>(rate) + 3u) / 4u;
            if (recordedTime >= target || target - recordedTime <= tolerance) low = middle;
            else high = middle - 1;
        }
        for (auto next = index_ + 1; next <= low; ++next) {
            PoseFrameHeader frame;
            if (!headerAt(next, frame)) return PosePlaybackStatus::UnavailableFrame;
            if (!(frame.route.flags & SampleAdmissible))
                return PosePlaybackStatus::UnsafeSample;
        }
        const bool changed = low != index_;
        auto frame = history_->before(anchor_, low);
        if (!frame) return PosePlaybackStatus::UnavailableFrame;
        selected_ = std::move(frame);
        index_ = low;
        presentation_ = {selectedKey(), {}};
        const auto& current = selected_.get()->header;
        if (target < current.elapsedNanoseconds && index_ + 1 < count_ && index_ < allowedThrough) {
            PoseFrameHeader older;
            if (!headerAt(index_ + 1, older)) return PosePlaybackStatus::UnavailableFrame;
            if (!(older.route.flags & SampleAdmissible)) return PosePlaybackStatus::UnsafeSample;
            if (!interpolatePosition(current, older, target, presentation_))
                return PosePlaybackStatus::UnavailableFrame;
        }
        if (index_ + 1 == count_) return PosePlaybackStatus::AtEnd;
        return changed ? PosePlaybackStatus::Ready : PosePlaybackStatus::Held;
    }

    bool hold(const GameTimeSnapshot& clock) {
        if (clock.elapsedNanoseconds < lastClock_) return false;
        lastClock_ = clock.elapsedNanoseconds;
        return true;
    }

    void reset() {
        selected_.release();
        history_ = nullptr;
        anchor_ = {};
        count_ = index_ = world_ = 0;
        newestTime_ = duration_ = elapsed_ = lastClock_ = 0;
        rateRemainder_ = 0;
        presentation_ = {};
    }
    const RecordedPoseFrame* selectedFrame() const { return selected_.get(); }
    PoseFrameKey selectedKey() const { return selected_ ? selected_.get()->header.key : PoseFrameKey{}; }
    PosePresentation presentation() const { return presentation_; }
    HistorySample appliedSample() const {
        auto sample = selected_ ? selected_.get()->header.route : HistorySample{};
        shiftPosition(sample.pose.position, presentation_.offset);
        return sample;
    }
    PoseFrameKey anchorKey() const { return anchor_; }
    bool headerAt(std::uint32_t index, PoseFrameHeader& out) const {
        return history_ && index < count_ && history_->copyHeaderBefore(anchor_, index, out);
    }
    std::uint32_t count() const { return count_; }
    std::uint32_t index() const { return index_; }
    std::uint64_t durationNanoseconds() const { return duration_; }
    std::uint64_t elapsedNanoseconds() const { return elapsed_; }

private:
    const PoseHistory* history_ = nullptr;
    PoseReadLease selected_;
    PoseFrameKey anchor_{};
    PosePresentation presentation_{};
    std::uint32_t count_ = 0;
    std::uint32_t index_ = 0;
    std::uint32_t world_ = 0;
    std::uint64_t newestTime_ = 0;
    std::uint64_t duration_ = 0;
    std::uint64_t elapsed_ = 0;
    std::uint64_t lastClock_ = 0;
    std::uint8_t rateRemainder_ = 0;
};

}

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
    std::uint16_t count = 0;
    NativePathPoint points[kMaxRenderPoints + 1]{};
};
static_assert(kMaxRenderPoints + 1 < 512);

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
    out.count = static_cast<std::uint16_t>(count);
    return count >= 2 ? NativePathStatus::Ready : NativePathStatus::Empty;
}

}

#include <span>
#include <array>

namespace self_recall::pure {

constexpr std::size_t kProbeWindowMaxSamples = 12;
constexpr float kProbeWindowMaxMeters = 3.0f;
constexpr float kProbeWindowMaxDropMeters = 1.0f;
constexpr float kProbeLiftMeters = 0.45f;
constexpr float kProbeMinSegmentMeters = 0.05f;

inline std::uint32_t nextRouteProbeStart(std::uint32_t index, std::uint32_t checked,
        std::uint32_t count, bool prefetch) {
    if (!count || index >= count - 1 || checked >= count - 1) return UINT32_MAX;
    if (checked > index) {
        if (!prefetch || checked - index > kProbeWindowMaxSamples) return UINT32_MAX;
        return checked;
    }
    return index;
}

enum class RouteProbeKind : std::uint8_t { Cast, Stationary, Climb, Steep, Unavailable, Vehicle };
struct RouteProbeSegment {
    totk::core::WorldPosition from{}, to{};
    std::uint8_t fromFlags = 0, toFlags = 0;
};
struct RouteProbePlan {
    RouteProbeKind kind = RouteProbeKind::Unavailable;
    std::uint32_t through = 0;
    std::array<RouteProbeSegment, kProbeWindowMaxSamples> segments{};
    unsigned count = 0;
};

inline totk::core::WorldPosition routeProbePoint(const Pose& pose) {
    const auto& r = pose.rotation.values;
    return {pose.position.x + kProbeLiftMeters * r[1],
            pose.position.y + kProbeLiftMeters * r[4],
            pose.position.z + kProbeLiftMeters * r[7]};
}

inline RouteProbePlan planRouteProbe(std::span<const HistorySample> samples, bool climbActive) {
    RouteProbePlan out{};
    if (samples.size() < 2) return out;
    const auto valid = [](const HistorySample& sample) {
        return (sample.flags & SampleAdmissible) && finitePose(sample.pose);
    };
    if (!valid(samples[0]) || !valid(samples[1])) return out;
    if (climbActive || ((samples[0].flags | samples[1].flags) & SampleClimb)) {
        out.kind = RouteProbeKind::Climb;
        out.through = 1;
        for (std::size_t i = 2; i < samples.size() && i <= kProbeWindowMaxSamples; ++i) {
            if (!valid(samples[i]) || (!climbActive &&
                !((samples[i - 1].flags | samples[i].flags) & SampleClimb))) break;
            out.through = static_cast<std::uint32_t>(i);
        }
        return out;
    }
    if ((samples[0].flags | samples[1].flags) & SampleControlStick) {
        out.kind = RouteProbeKind::Vehicle;
        out.through = 1;
        for (std::size_t i = 2; i < samples.size() && i <= kProbeWindowMaxSamples; ++i) {
            if (!valid(samples[i]) ||
                !((samples[i - 1].flags | samples[i].flags) & SampleControlStick)) break;
            out.through = static_cast<std::uint32_t>(i);
        }
        return out;
    }
    const auto start = samples[0].pose.position;
    auto end = start;
    float distance = 0;
    for (std::size_t i = 1; i < samples.size() && i <= kProbeWindowMaxSamples; ++i) {
        if (!valid(samples[i]) || (samples[i].flags & (SampleClimb | SampleControlStick))) break;
        end = samples[i].pose.position;
        out.through = static_cast<std::uint32_t>(i);
        const auto from = routeProbePoint(samples[i - 1].pose);
        const auto to = routeProbePoint(samples[i].pose);
        const auto step = distance3(from, to);
        if (!std::isfinite(step)) return {};
        distance += step;
        if (step > 0)
            out.segments[out.count++] = {from, to, samples[i - 1].flags, samples[i].flags};
        if (distance >= kProbeWindowMaxMeters) break;
    }
    const auto span = distance3(start, end);
    const auto rise = std::abs(start.y - end.y);
    if (!std::isfinite(span) || !out.through) return {};
    if (rise > kProbeWindowMaxDropMeters) out.kind = RouteProbeKind::Steep;
    else if (distance < kProbeMinSegmentMeters) out.kind = RouteProbeKind::Stationary;
    else out.kind = RouteProbeKind::Cast;
    return out;
}

}

namespace self_recall::pure {
inline bool tolerableRouteContact(const RouteProbeSegment& segment,
        totk::core::WorldPosition hit, totk::core::WorldPosition normal) {
    const auto step = distance3(segment.from, segment.to);
    const auto start = distance3(segment.from, hit);
    const auto end = distance3(segment.to, hit);
    const auto length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (!std::isfinite(step) || !std::isfinite(start) || !std::isfinite(end) ||
        !std::isfinite(length) || length < 0.9f || length > 1.1f) return false;
    const float inward = -((segment.to.x - segment.from.x) * normal.x +
        (segment.to.y - segment.from.y) * normal.y + (segment.to.z - segment.from.z) * normal.z) / length;
    if (start <= 0.005f && (inward <= 0.001f || (step <= 0.1f && inward <= 0.06f))) return true;
    return normal.y / length >= 0.65f && step <= 0.1f && end <= 0.02f;
}
}
