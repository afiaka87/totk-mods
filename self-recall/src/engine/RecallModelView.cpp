#include "RecallModelView.hpp"

#include <cstring>

#include "RecallPoseTransport.hpp"

namespace self_recall::model {
namespace {

constexpr std::uintptr_t kModelNWVtable = 0x045C0570;
constexpr std::uintptr_t kSearchBoneIndex = 0x02A40628;
constexpr std::size_t kUnitFlags = 0x12;
constexpr std::size_t kSkeleton = 0x170;
constexpr std::size_t kRenderOrigin = 0x338;
constexpr std::size_t kOriginFlags = 0x355;
constexpr std::size_t kSkeletonWorldMatrices = 0x20;
constexpr std::size_t kResourceBoneCount = 0x38;
constexpr std::size_t kBoneVisibility = 0x140;
constexpr std::size_t kMaterialVisibility = 0x148;
constexpr std::size_t kMaterialCount = 0x16A;

template <class T>
T read(const void* base, std::size_t offset) {
    T result;
    std::memcpy(&result, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return result;
}

}  // namespace

ViewStatus describe(std::uintptr_t mainBase, const void* unit, View& out) {
    out = {};
    if (!unit || read<std::uintptr_t>(unit, 0) != mainBase + kModelNWVtable)
        return ViewStatus::UnsupportedType;
    const auto* skeleton = read<const void*>(unit, kSkeleton);
    const auto* resource = skeleton ? read<const void*>(skeleton, 0) : nullptr;
    const auto* matrices = skeleton ? read<const void*>(skeleton, kSkeletonWorldMatrices) : nullptr;
    if (!resource || !matrices) return ViewStatus::MissingSkeleton;
    const auto boneCount = read<std::uint16_t>(resource, kResourceBoneCount);
    if (!boneCount) return ViewStatus::MissingSkeleton;
    if (boneCount > pure::kPoseBoneLimit) return ViewStatus::BoneLimitExceeded;
    const auto materialCount = read<std::uint16_t>(unit, kMaterialCount);
    if (materialCount > pure::kPoseMaterialLimit) return ViewStatus::MaterialLimitExceeded;
    out.boneVisibility = read<const std::uint32_t*>(unit, kBoneVisibility);
    out.materialVisibility = read<const std::uint32_t*>(unit, kMaterialVisibility);
    if (!out.boneVisibility || (materialCount && !out.materialVisibility))
        return ViewStatus::MissingVisibility;

    out.identity = {reinterpret_cast<std::uintptr_t>(unit),
                    reinterpret_cast<std::uintptr_t>(skeleton),
                    reinterpret_cast<std::uintptr_t>(resource), boneCount, materialCount};
    out.boneBytes = matrices;
    out.pose.identity = {out.identity.unit, out.identity.skeleton,
                         out.identity.resource, 0, boneCount, 0, materialCount};
    out.pose.visibility = read<std::uint32_t>(unit, kUnitFlags);
    out.pose.originRelative = read<std::uint8_t>(unit, kOriginFlags) & 1u;
    std::memcpy(out.pose.renderOrigin,
                static_cast<const std::uint8_t*>(unit) + kRenderOrigin,
                sizeof(out.pose.renderOrigin));

    const char* wristName = "Wrist_R";
    using SearchBone = std::uint32_t (*)(const void*, const char* const*);
    const auto index = reinterpret_cast<SearchBone>(mainBase + kSearchBoneIndex)(unit, &wristName);
    if (index < boneCount) out.wristIndex = static_cast<std::int32_t>(index);
    return ViewStatus::Ready;
}

bool copyWrist(const View& view, float out[12]) {
    if (!view.boneBytes || view.wristIndex < 0 ||
        view.wristIndex >= view.identity.boneCount) return false;
    pure::RecordedBoneMatrix bone;
    std::memcpy(&bone, static_cast<const std::uint8_t*>(view.boneBytes) +
                       static_cast<std::size_t>(view.wristIndex) * sizeof(bone), sizeof(bone));
    return pure::boneToWorldMatrix(bone, view.pose, out);
}

CaptureReport recordCompleted(const pure::PoseFrameHeader& header,
                              std::span<const View> views,
                              CaptureWorkspace& workspace,
                              pure::PoseHistory& history) {
    if (views.empty() || views.size() != header.modelCount ||
        views.size() > pure::kPoseModelLimit)
        return {CaptureStatus::ModelCountMismatch, {}};

    std::uint32_t totalBones = 0;
    std::uint32_t totalMaterials = 0;
    for (const auto& view : views) {
        if (!view.boneBytes || !view.identity.boneCount)
            return {CaptureStatus::BoneRangeMismatch, {}};
        totalBones += view.identity.boneCount;
        totalMaterials += view.identity.materialCount;
        if (totalBones > pure::kPoseBoneLimit)
            return {CaptureStatus::BoneRangeMismatch, {}};
        if (totalMaterials > pure::kPoseMaterialLimit)
            return {CaptureStatus::MaterialRangeMismatch, {}};
        if (!view.boneVisibility || (view.identity.materialCount && !view.materialVisibility))
            return {CaptureStatus::MissingVisibility, {}};
    }
    if (totalBones != header.boneCount)
        return {CaptureStatus::BoneRangeMismatch, {}};
    if (totalMaterials != header.materialCount)
        return {CaptureStatus::MaterialRangeMismatch, {}};

    pure::PoseFrameInput input{header, workspace.models, workspace.bones};
    input.header.haveWrist = false;
    std::uint16_t nextBone = 0;
    std::uint16_t nextMaterial = 0;
    const auto copyVisibility = [](const std::uint32_t* source, std::uint16_t count,
                                   std::uint32_t* destination, std::uint16_t first) {
        for (std::uint16_t bit = 0; bit < count; ++bit) {
            if (!pure::visibilityBit(source, bit)) continue;
            const auto index = first + bit;
            destination[index / 32] |= 1u << (index % 32);
        }
    };
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& view = views[i];
        auto& destination = workspace.models[i];
        destination = view.pose;
        destination.identity = {view.identity.unit, view.identity.skeleton,
                                view.identity.resource, nextBone, view.identity.boneCount,
                                nextMaterial, view.identity.materialCount};
        copyVisibility(view.boneVisibility, view.identity.boneCount, input.visible.bones, nextBone);
        copyVisibility(view.materialVisibility, view.identity.materialCount,
                       input.visible.materials, nextMaterial);
        std::memcpy(workspace.bones + nextBone, view.boneBytes,
                    view.identity.boneCount * sizeof(pure::RecordedBoneMatrix));
        nextBone = static_cast<std::uint16_t>(nextBone + view.identity.boneCount);
        nextMaterial = static_cast<std::uint16_t>(nextMaterial + view.identity.materialCount);
        if (!input.header.haveWrist && view.wristIndex >= 0) {
            if (view.wristIndex >= view.identity.boneCount ||
                !pure::boneToWorldMatrix(workspace.bones[destination.identity.firstBone +
                                                       view.wristIndex],
                                         destination, input.header.wristMatrix))
                return {CaptureStatus::MissingWrist, {}};
            input.header.haveWrist = true;
        }
    }
    if (!input.header.haveWrist) return {CaptureStatus::MissingWrist, {}};
    const auto report = history.record(input);
    return {report.status == pure::PoseRecordStatus::Recorded ? CaptureStatus::Recorded
                                                             : CaptureStatus::HistoryRejected,
            report};
}

}  // namespace self_recall::model
