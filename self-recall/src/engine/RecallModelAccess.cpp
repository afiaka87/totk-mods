#include "RecallModelEngine.hpp"

#include <cstring>

#include "RecallPlayback.hpp"

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
T readViewField(const void* base, std::size_t offset) {
    T result;
    std::memcpy(&result, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return result;
}

}

ViewStatus describe(std::uintptr_t mainBase, const void* unit, View& out) {
    out = {};
    if (!unit || readViewField<std::uintptr_t>(unit, 0) != mainBase + kModelNWVtable)
        return ViewStatus::UnsupportedType;
    const auto* skeleton = readViewField<const void*>(unit, kSkeleton);
    const auto* resource = skeleton ? readViewField<const void*>(skeleton, 0) : nullptr;
    const auto* matrices = skeleton ? readViewField<const void*>(skeleton, kSkeletonWorldMatrices) : nullptr;
    if (!resource || !matrices) return ViewStatus::MissingSkeleton;
    const auto boneCount = readViewField<std::uint16_t>(resource, kResourceBoneCount);
    if (!boneCount) return ViewStatus::MissingSkeleton;
    if (boneCount > pure::kPoseBoneLimit) return ViewStatus::BoneLimitExceeded;
    const auto materialCount = readViewField<std::uint16_t>(unit, kMaterialCount);
    if (materialCount > pure::kPoseMaterialLimit) return ViewStatus::MaterialLimitExceeded;
    out.boneVisibility = readViewField<const std::uint32_t*>(unit, kBoneVisibility);
    out.materialVisibility = readViewField<const std::uint32_t*>(unit, kMaterialVisibility);
    if (!out.boneVisibility || (materialCount && !out.materialVisibility))
        return ViewStatus::MissingVisibility;

    out.identity = {reinterpret_cast<std::uintptr_t>(unit),
                    reinterpret_cast<std::uintptr_t>(skeleton),
                    reinterpret_cast<std::uintptr_t>(resource), boneCount, materialCount};
    out.boneBytes = matrices;
    out.pose.identity = {out.identity.unit, out.identity.skeleton,
                         out.identity.resource, 0, boneCount, 0, materialCount};
    out.pose.visibility = readViewField<std::uint32_t>(unit, kUnitFlags);
    out.pose.originRelative = readViewField<std::uint8_t>(unit, kOriginFlags) & 1u;
    std::memcpy(out.pose.renderOrigin,
                static_cast<const std::uint8_t*>(unit) + kRenderOrigin,
                sizeof(out.pose.renderOrigin));

    const char* wristName = "Wrist_R";
    using SearchBone = std::uint32_t (*)(const void*, const char* const*);
    const auto index = reinterpret_cast<SearchBone>(mainBase + kSearchBoneIndex)(unit, &wristName);
    if (index < boneCount) out.wristIndex = static_cast<std::int32_t>(index);
    return ViewStatus::Ready;
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

}

namespace self_recall::model {
namespace {

constexpr std::uintptr_t kActorLinkGetReference = 0x00753530;
constexpr std::uintptr_t kBaseProcReferenceSetProc = 0x0086A288;

detail::NativeActorReferenceResult resolve(std::uintptr_t mainBase, const void* link) {
    if (!mainBase || !link) return {};
    using Resolve = detail::NativeActorReferenceResult (*)(const void*);
    return reinterpret_cast<Resolve>(mainBase + kActorLinkGetReference)(link);
}

}

ScopedActorReference::ScopedActorReference(std::uintptr_t mainBase, const void* actorLink)
    : mainBase_(mainBase), reference_(resolve(mainBase, actorLink)) {}

ScopedActorReference::~ScopedActorReference() {
    if (!mainBase_ || !reference_.actor) return;
    using Clear = void (*)(detail::NativeActorReferenceResult*, const void*);
    reinterpret_cast<Clear>(mainBase_ + kBaseProcReferenceSetProc)(&reference_, nullptr);
}

}

namespace self_recall::model {
namespace {
template<class T> T readLeaseField(const void* p, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
    return value;
}

struct BinderReferenceArgument {
    std::uintptr_t vtable;
    std::uint8_t kind = 3, synchronous = 1, required = 1;
    std::byte padding[5]{};
    const char* requester = "SelfRecallEquipment";
    const void* source;
    std::uintptr_t reserved = 0;
};
static_assert(sizeof(BinderReferenceArgument) == 40);
static_assert(offsetof(BinderReferenceArgument, source) == 0x18);
}

bool ResourceLease::retain(std::uintptr_t mainBase, const void* sourceBinder) {
    if (mainBase_ || !mainBase || !sourceBinder) return false;
    using Get = const void* (*)(const void*);
    const auto get = reinterpret_cast<Get>(mainBase + 0x00B51804);
    const auto* sourceResource = get(sourceBinder);
    if (!sourceResource || !readLeaseField<const void*>(sourceBinder, 8)) return false;
    using Construct = void (*)(void*);
    reinterpret_cast<Construct>(mainBase + 0x00BC7A48)(binder_);
    mainBase_ = mainBase;
    const BinderReferenceArgument argument{mainBase + 0x045C80F0, 3, 1, 1, {},
                                          "SelfRecallEquipment", sourceBinder, 0};
    const char* empty = "";
    using Retain = const void* (*)(void*, const char* const*, const void*, int*);
    resource_ = reinterpret_cast<Retain>(mainBase + 0x0076F8F0)(binder_, &empty, &argument, nullptr);
    if (resource_ != sourceResource ||
        readLeaseField<const void*>(binder_, 8) != readLeaseField<const void*>(sourceBinder, 8)) {
        release();
        return false;
    }
    return true;
}

void ResourceLease::release() {
    if (!mainBase_) return;
    using Destroy = void (*)(void*);
    reinterpret_cast<Destroy>(mainBase_ + 0x00770B8C)(binder_);
    resource_ = nullptr;
    mainBase_ = 0;
}

}
