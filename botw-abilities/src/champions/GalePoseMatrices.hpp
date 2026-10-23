// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "GalePose.hpp"
#include <array>

namespace botw_abilities::champions {
struct PoseMatrix {
    std::array<float, 16> values{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float& at(unsigned row, unsigned column) { return values[column * 4 + row]; }
    float at(unsigned row, unsigned column) const { return values[column * 4 + row]; }
};
static_assert(sizeof(PoseMatrix) == 64);

inline bool finiteAffine(const PoseMatrix& matrix) {
    for (float value : matrix.values) if (!std::isfinite(value)) return false;
    return matrix.at(3,0) == 0 && matrix.at(3,1) == 0 && matrix.at(3,2) == 0 && matrix.at(3,3) == 1;
}

inline bool composePose(const PoseMatrix& parent, const PoseMatrix& local, PoseMatrix& out) {
    if (!finiteAffine(parent) || !finiteAffine(local)) return false;
    PoseMatrix result;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 4; ++column) {
            double value = column == 3 ? parent.at(row,3) : 0;
            for (unsigned axis = 0; axis < 3; ++axis)
                value += double(parent.at(row,axis)) * local.at(axis,column);
            result.at(row,column) = float(value);
        }
    if (!finiteAffine(result)) return false;
    out = result;
    return true;
}

inline bool inversePose(const PoseMatrix& source, PoseMatrix& out) {
    if (!finiteAffine(source)) return false;
    const double a = source.at(0,0), b = source.at(0,1), c = source.at(0,2),
                 d = source.at(1,0), e = source.at(1,1), f = source.at(1,2),
                 g = source.at(2,0), h = source.at(2,1), i = source.at(2,2);
    const double determinant = a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12) return false;
    const double inverse[]{(e*i-f*h)/determinant, (c*h-b*i)/determinant, (b*f-c*e)/determinant,
        (f*g-d*i)/determinant, (a*i-c*g)/determinant, (c*d-a*f)/determinant,
        (d*h-e*g)/determinant, (b*g-a*h)/determinant, (a*e-b*d)/determinant};
    PoseMatrix result;
    for (unsigned row = 0; row < 3; ++row) {
        double translation = 0;
        for (unsigned column = 0; column < 3; ++column) {
            result.at(row,column) = float(inverse[row*3+column]);
            translation -= inverse[row*3+column] * source.at(column,3);
        }
        result.at(row,3) = float(translation);
    }
    if (!finiteAffine(result)) return false;
    out = result;
    return true;
}

inline bool localPoseMatrix(const LocalPose& local, PoseMatrix& out) {
    double norm = 0;
    for (float value : local.rotation) norm += double(value) * value;
    if (!std::isfinite(norm) || std::abs(norm - 1) > .001) return false;
    const auto x = local.rotation[0], y = local.rotation[1], z = local.rotation[2], w = local.rotation[3];
    PoseMatrix result;
    result.at(0,0) = 1-2*(y*y+z*z); result.at(0,1) = 2*(x*y-z*w); result.at(0,2) = 2*(x*z+y*w);
    result.at(1,0) = 2*(x*y+z*w); result.at(1,1) = 1-2*(x*x+z*z); result.at(1,2) = 2*(y*z-x*w);
    result.at(2,0) = 2*(x*z-y*w); result.at(2,1) = 2*(y*z+x*w); result.at(2,2) = 1-2*(x*x+y*y);
    for (unsigned axis = 0; axis < 3; ++axis) result.at(axis,3) = local.position[axis];
    if (!finiteAffine(result)) return false;
    out = result;
    return true;
}

template<class Parent>
bool buildPoseHierarchy(std::span<const LocalPose> locals, Parent parent, std::span<PoseMatrix> out) {
    if (locals.empty() || out.size() < locals.size()) return false;
    for (unsigned bone = 0; bone < locals.size(); ++bone) {
        const auto ancestor = parent(bone);
        if (bone == 0 ? ancestor != -1 : ancestor < 0 || unsigned(ancestor) >= bone) return false;
        PoseMatrix local;
        if (!localPoseMatrix(locals[bone], local)) return false;
        if (bone == 0) out[bone] = local;
        else if (!composePose(out[unsigned(ancestor)], local, out[bone])) return false;
    }
    return true;
}

inline bool followPoseParent(const PoseMatrix& current, const PoseMatrix& oldParent,
                             const PoseMatrix& newParent, PoseMatrix& out) {
    PoseMatrix inverse, relative;
    return inversePose(oldParent, inverse) && composePose(inverse, current, relative) &&
           composePose(newParent, relative, out);
}

inline bool anchorPose(std::span<PoseMatrix> matrices, const PoseMatrix& root) {
    if (matrices.empty()) return false;
    const auto original = matrices[0];
    for (auto& matrix : matrices)
        if (!followPoseParent(matrix, original, root, matrix)) return false;
    return true;
}

// Named clothing bones keep their live bind offsets; extra cloth bones inherit their parent's change.
template<class Read, class NameIndex, class Parent>
bool retargetPose(unsigned count, Read read, NameIndex nameIndex, Parent parent,
                  std::span<const PoseMatrix> oldBody, std::span<const PoseMatrix> newBody,
                  std::span<PoseMatrix> out) {
    if (!count || oldBody.size() != newBody.size() || out.size() < count) return false;
    for (unsigned bone = 0; bone < count; ++bone) {
        PoseMatrix current;
        if (!read(bone, current)) return false;
        const int source = nameIndex(bone);
        if (source >= 0) {
            if (unsigned(source) >= oldBody.size() ||
                !followPoseParent(current, oldBody[unsigned(source)], newBody[unsigned(source)], out[bone])) return false;
        } else {
            const int ancestor = parent(bone);
            PoseMatrix oldParent;
            if (ancestor < 0 || unsigned(ancestor) >= bone || !read(unsigned(ancestor), oldParent) ||
                !followPoseParent(current, oldParent, out[unsigned(ancestor)], out[bone])) return false;
        }
    }
    return true;
}

template<class NameIndex, class Parent>
bool retargetPose(std::span<const PoseMatrix> live, NameIndex nameIndex, Parent parent,
                  std::span<const PoseMatrix> oldBody, std::span<const PoseMatrix> newBody,
                  std::span<PoseMatrix> out) {
    return retargetPose(unsigned(live.size()), [&](unsigned bone, PoseMatrix& matrix) {
        matrix = live[bone]; return true;
    }, nameIndex, parent, oldBody, newBody, out);
}

inline bool rebasePoseOrigin(PoseMatrix& matrix, const std::array<float, 3>& from,
                              const std::array<float, 3>& to) {
    auto result = matrix;
    for (unsigned axis = 0; axis < 3; ++axis)
        result.at(axis,3) = float(double(matrix.at(axis,3)) + double(from[axis]) - double(to[axis]));
    if (!finiteAffine(result)) return false;
    matrix = result;
    return true;
}
} // namespace botw_abilities::champions
