#pragma once
#include "RecallPoseTransport.hpp"

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
