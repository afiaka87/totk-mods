#pragma once

#include <cmath>
#include <cstring>

#include "RecallPoseHistory.hpp"

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

}  // namespace self_recall::pure
