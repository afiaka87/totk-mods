#pragma once

#include <cstdint>
#include <span>

#include "RecallPoseData.hpp"

namespace self_recall::model {

struct Identity {
    std::uintptr_t unit = 0;
    std::uintptr_t skeleton = 0;
    std::uintptr_t resource = 0;
    std::uint16_t boneCount = 0;
    std::uint16_t materialCount = 0;
    bool operator==(const Identity&) const = default;
};

struct View {
    Identity identity{};
    pure::RecordedModelPose pose{};
    const void* boneBytes = nullptr;
    const std::uint32_t* boneVisibility = nullptr;
    const std::uint32_t* materialVisibility = nullptr;
    std::int32_t wristIndex = -1;
};

enum class ViewStatus : std::uint8_t {
    Ready,
    UnsupportedType,
    MissingSkeleton,
    BoneLimitExceeded,
    MaterialLimitExceeded,
    MissingVisibility,
};

ViewStatus describe(std::uintptr_t mainBase, const void* unit, View& out);

struct CaptureWorkspace {
    pure::RecordedModelPose models[pure::kPoseModelLimit]{};
    pure::RecordedBoneMatrix bones[pure::kPoseBoneLimit]{};
};

enum class CaptureStatus : std::uint8_t {
    Recorded,
    ModelCountMismatch,
    BoneRangeMismatch,
    MissingWrist,
    HistoryRejected,
    MaterialRangeMismatch,
    MissingVisibility,
};

struct CaptureReport {
    CaptureStatus status = CaptureStatus::HistoryRejected;
    pure::PoseRecordReport history{};
};

CaptureReport recordCompleted(const pure::PoseFrameHeader& header,
                              std::span<const View> views,
                              CaptureWorkspace& workspace,
                              pure::PoseHistory& history);

}

#include <type_traits>

namespace self_recall::model {
namespace detail {

struct NativeActorReferenceResult {
    void* actor = nullptr;
    std::uint8_t counted = 0;
    std::uint8_t reserved[7]{};
    ~NativeActorReferenceResult() {}
};
static_assert(sizeof(NativeActorReferenceResult) == 16);
static_assert(!std::is_trivially_destructible_v<NativeActorReferenceResult>);

}

class ScopedActorReference {
public:
    ScopedActorReference(std::uintptr_t mainBase, const void* actorLink);
    ~ScopedActorReference();
    ScopedActorReference(const ScopedActorReference&) = delete;
    ScopedActorReference& operator=(const ScopedActorReference&) = delete;
    ScopedActorReference(ScopedActorReference&&) = delete;
    ScopedActorReference& operator=(ScopedActorReference&&) = delete;

    void* get() const { return reference_.actor; }
    explicit operator bool() const { return get() != nullptr; }

private:
    std::uintptr_t mainBase_ = 0;
    detail::NativeActorReferenceResult reference_{};
};

}

#include <cstring>
namespace self_recall::model {

enum class CompletedQueueStatus { Ready, MissingBody, MissingQueue, Limit };
inline CompletedQueueStatus inspectCompletedQueue(const void* queue, std::span<View> views,
                                                   unsigned bodyModels) {
    if (!queue || !bodyModels || bodyModels > views.size()) return CompletedQueueStatus::MissingQueue;
    const auto read = []<class T>(const void* p, std::size_t offset) {
        T value;
        std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
        return value;
    };
    const auto singlesCount = read.operator()<std::uint32_t>(queue, 0x20);
    const auto groupsCount = read.operator()<std::uint32_t>(queue, 0x58);
    if (singlesCount > 16384 || groupsCount > 16384) return CompletedQueueStatus::Limit;
    const auto* singles = read.operator()<const void* const*>(queue, 0x28);
    const auto* groups = read.operator()<const void* const*>(queue, 0x60);
    if ((!singles && singlesCount) || (!groups && groupsCount)) return CompletedQueueStatus::MissingQueue;
    for (auto& view : views) view.pose.queueAdmission = 0;
    bool bodyPresent = false;
    std::uint32_t visited = 0;
    const auto walk = [&](const void* node) {
        for (; node; node = read.operator()<const void*>(node, 8)) {
            if (++visited > 65536) return false;
            const auto unit = read.operator()<std::uintptr_t>(node, 0);
            for (unsigned j = 0; j < views.size(); ++j) {
                if (views[j].identity.unit != unit) continue;
                if (read.operator()<std::uint8_t>(node, 0x1E) & 0x40u)
                    views[j].pose.queueAdmission = 1;
                if (j < bodyModels) bodyPresent = true;
            }
        }
        return true;
    };
    for (std::uint32_t i = 0; i < singlesCount; ++i) {
        if (!singles[i]) return CompletedQueueStatus::MissingQueue;
        const auto* entries = read.operator()<const void* const*>(singles[i], 0x28);
        if (!entries || !entries[0]) return CompletedQueueStatus::MissingQueue;
        if (!walk(entries[0])) return CompletedQueueStatus::Limit;
    }
    for (std::uint32_t i = 0; i < groupsCount; ++i)
        if (!walk(groups[i])) return CompletedQueueStatus::Limit;
    return bodyPresent ? CompletedQueueStatus::Ready : CompletedQueueStatus::MissingBody;
}

}

#include <cstddef>

namespace self_recall::model {

class ResourceLease {
public:
    ResourceLease() = default;
    ResourceLease(const ResourceLease&) = delete;
    ResourceLease& operator=(const ResourceLease&) = delete;
    bool retain(std::uintptr_t mainBase, const void* sourceBinder);
    void release();
    const void* resource() const { return resource_; }
private:
    alignas(8) std::byte binder_[40]{};
    std::uintptr_t mainBase_ = 0;
    const void* resource_ = nullptr;
};

}

#include "RecallRender.hpp"
#include <array>
#include <atomic>

namespace self_recall::equipment::detail {
constexpr unsigned kAssetLimit = 64;
constexpr unsigned kResourceLimit = pure::kPoseModelLimit;
using Life = pure::ArchiveLife;
struct Asset {
    std::atomic<Life> life{Life::Empty};
    std::atomic<const void*> root{nullptr};
    const void* sourceRoot = nullptr;
    std::uint32_t actorId = 0, world = 0;
    std::uint16_t count = 0, resourceCount = 0;
    pure::PoseFrameKey last{};
    unsigned appearance = 0;
    std::array<model::Identity, pure::kPoseModelLimit> source{}, copy{};
    std::array<model::ResourceLease, kResourceLimit> resources;
};
}

namespace self_recall::equipment {
void install(std::uintptr_t mainBase);
bool remap(const void* component, std::uint32_t actorId, std::uint32_t world,
           const void* playerComponent, std::span<model::View> source);
void beginRecord(std::uint32_t historyGeneration);
bool recorded(const pure::RecordedPoseFrame& frame);
bool selectAppearance(const pure::RecordedPoseFrame& frame);
void recordEffects(pure::PoseFrameHeader& header, std::span<const model::View> views);
void collectExpired(const pure::PoseHistory& history, std::uint32_t world);
bool publishedModel(const void* unit);

bool resolve(const pure::RecordedModelIdentity& token, const void* scene,
             const void* playerComponent, model::View& view, const void*& root);
}

#include <initializer_list>

namespace self_recall::model {

enum class EquipmentLinkKind { Dynamic, Static, Attachment, ExtraAttachment };
inline constexpr std::size_t kOwnedActorLimit = 1 + 8 + 12 + 8 + 8 + 1 + 2;

template<class ReadPointer, class ReadByte>
const void* fusedEquipmentLink(const void* components, ReadPointer&& readPointer,
                               ReadByte&& readByte) {
    const auto* equipment = components ? readPointer(components, 0x208) : nullptr;
    return equipment && readByte(equipment, 0x50C)
        ? static_cast<const std::byte*>(equipment) + 0xB0 : nullptr;
}

inline bool isEquipmentParent(const void* parent, const void* player,
                              std::span<const void* const> owned) {
    if (!parent) return false;
    if (parent == player) return true;
    for (const auto* actor : owned) if (parent == actor) return true;
    return false;
}

template<class Visit>
bool visitEquipmentLinks(const void* equipment, Visit&& visit) {
    if (!equipment) return true;
    const auto* bytes = static_cast<const std::byte*>(equipment);
    for (unsigned i = 0; i < 8; ++i)
        if (!visit(bytes + 0x20 + 0x18 * i, EquipmentLinkKind::Dynamic, i)) return false;
    for (unsigned i = 0; i < 12; ++i)
        if (!visit(bytes + 0xE0 + 0x18 * i, EquipmentLinkKind::Static, i)) return false;
    for (unsigned i = 0; i < 8; ++i)
        if (!visit(bytes + 0x580 + 0x100 * i, EquipmentLinkKind::Attachment, i)) return false;
    return visit(bytes + 0x3E90, EquipmentLinkKind::ExtraAttachment, 0);
}

template<class ReadPointer, class MatchesParent>
bool hasBoundEquipmentParent(const void* components, ReadPointer&& readPointer,
                             MatchesParent&& matchesParent) {
    if (!components) return false;
    for (const auto offset : {std::size_t{0x18}, std::size_t{0x420}}) {
        const auto* bind = readPointer(components, offset);
        if (bind && matchesParent(static_cast<const std::byte*>(bind) + 0x70)) return true;
    }
    return false;
}

}

#include "totk/engine/Runtime.hpp"

namespace self_recall::actor_model {
constexpr std::size_t kModelComponent = 0x10;
constexpr std::size_t kModelFromComponent = 0x28;
constexpr std::size_t kEquipmentUser = 0x230;
constexpr std::size_t kModelController = 0x280;
constexpr std::size_t kPlayerComponent = 0x3A8;
constexpr std::size_t kModelRoot = 0x1F8;
constexpr std::size_t kModelCount = 0x20;
constexpr std::size_t kModelEntries = 0x28;
constexpr std::size_t kActorId = 0x10;

template <class T>
T read(const void* base, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}
inline const void* at(const void* base, std::size_t offset) {
    return static_cast<const std::uint8_t*>(base) + offset;
}
inline const void* registry(const void* actor) {
    return read<const void*>(actor, totk::engine::layout::kActorComponentRegistry);
}
inline const void* actorModel(const void* actor) {
    const auto* components = registry(actor);
    const auto* component = components ? read<const void*>(components, kModelComponent) : nullptr;
    return component ? read<const void*>(component, kModelFromComponent) : nullptr;
}
inline pure::Pose actorPose(const void* actor) {
    pure::Pose pose{};
    std::memcpy(&pose.position, at(actor, totk::engine::layout::kActorPosition), sizeof(pose.position));
    std::memcpy(&pose.rotation, at(actor, totk::engine::layout::kActorRotation), sizeof(pose.rotation));
    return pose;
}

}

#include <optional>
#include "RecallRuntimeEngine.hpp"

namespace self_recall::pose_recorder::detail {
using namespace actor_model;
using namespace offsets121::pose_recorder;
constexpr std::size_t kActorLimit = model::kOwnedActorLimit;
enum class Rejection : unsigned {
    MissingPlayer = 1, MissingRoot, UnpairedRoot, ActorLimit, ModelLimit,
    UnsupportedModel, QueueLimit, MissingQueue, CaptureRejected, ControlChanged, EquipmentArchive,
};

struct OwnedModelCollection {
    explicit OwnedModelCollection(std::uintptr_t mainBase) : mainBase_(mainBase) {}
private:
    std::uintptr_t mainBase_;
public:
    std::array<std::optional<model::ScopedActorReference>, kActorLimit> references;
    std::array<const void*, kActorLimit> actors{};
    std::array<model::View, pure::kPoseModelLimit> views{};
    std::array<const void*, kActorLimit> roots{};
    struct EquipmentGroup {
        const void* component = nullptr;
        std::uint32_t actorId = 0;
        std::uint16_t first = 0, count = 0;
    };
    std::array<EquipmentGroup, kActorLimit> equipmentGroups{};
    std::uint16_t equipmentCount = 0;
    std::uint16_t rootCount = 0;
    std::uint16_t actorCount = 0;
    std::uint16_t modelCount = 0;
    std::uint16_t boneCount = 0;
    std::uint16_t materialCount = 0;
    std::uint16_t bodyModels = 0;
    Rejection error = Rejection::MissingRoot;

    bool appendModel(const void* actor, bool body) {
        const auto firstModel = modelCount;
        const auto* root = actorModel(actor);
        if (!root) return !body;
        const auto count = read<std::int32_t>(root, kModelCount);
        const auto* entries = read<const void* const*>(root, kModelEntries);
        if (count < 0 || count > pure::kPoseModelLimit || (!entries && count) || (!count && body)) {
            error = Rejection::ModelLimit;
            return false;
        }
        for (std::int32_t i = 0; i < count; ++i) {
            const auto* entry = entries[i];
            const auto* unit = entry ? read<const void*>(entry, 0) : nullptr;
            if (!unit) { error = Rejection::UnsupportedModel; return false; }
            bool duplicate = false;
            for (std::uint16_t j = 0; j < modelCount; ++j)
                duplicate |= views[j].identity.unit == reinterpret_cast<std::uintptr_t>(unit);
            if (duplicate) continue;
            if (modelCount == pure::kPoseModelLimit) { error = Rejection::ModelLimit; return false; }
            auto& view = views[modelCount];
            if (model::describe(mainBase_, unit, view) != model::ViewStatus::Ready) {
                error = Rejection::UnsupportedModel;
                return false;
            }
            if (boneCount + view.identity.boneCount > pure::kPoseBoneLimit ||
                materialCount + view.identity.materialCount > pure::kPoseMaterialLimit) {
                error = Rejection::ModelLimit;
                return false;
            }
            boneCount = static_cast<std::uint16_t>(boneCount + view.identity.boneCount);
            materialCount = static_cast<std::uint16_t>(materialCount + view.identity.materialCount);
            ++modelCount;
        }
        if (body) bodyModels = modelCount;
        else if (modelCount != firstModel) {
            const auto* components = registry(actor);
            equipmentGroups[equipmentCount++] = {
                read<const void*>(components, kModelComponent), read<std::uint32_t>(actor, kActorId),
                firstModel, static_cast<std::uint16_t>(modelCount - firstModel)};
        }
        bool haveRoot = false;
        for (unsigned i = 0; i < rootCount; ++i) haveRoot |= roots[i] == root;
        if (count && !haveRoot) {
            if (rootCount == roots.size()) { error = Rejection::ActorLimit; return false; }
            roots[rootCount++] = root;
        }
        return true;
    }

    bool archiveEquipment(const void* player, std::uint32_t world) {
        const auto* playerComponent = read<const void*>(registry(player), kModelComponent);
        for (unsigned i = 0; i < equipmentCount; ++i) {
            const auto& group = equipmentGroups[i];
            if (!equipment::remap(group.component, group.actorId, world, playerComponent,
                                  {views.data() + group.first, group.count})) {
                error = Rejection::EquipmentArchive;
                return false;
            }
        }
        return true;
    }

    bool useHistoricalEquipment(const void* player, const void* scene,
                                const pure::RecordedPoseFrame& recorded) {
        error = Rejection::EquipmentArchive;
        if (!bodyModels || recorded.header.bodyModelCount != bodyModels) return false;
        modelCount = bodyModels;
        rootCount = 1;
        roots[0] = actorModel(player);
        boneCount = materialCount = 0;
        for (unsigned i = 0; i < bodyModels; ++i) {
            boneCount = static_cast<std::uint16_t>(boneCount + views[i].identity.boneCount);
            materialCount = static_cast<std::uint16_t>(materialCount + views[i].identity.materialCount);
        }
        const auto* playerComponent = read<const void*>(registry(player), kModelComponent);
        for (unsigned i = bodyModels; i < recorded.header.modelCount; ++i) {
            const void* root = nullptr;
            auto& view = views[i];
            if (!equipment::resolve(recorded.models[i].identity, scene, playerComponent, view, root)) return false;
            boneCount = static_cast<std::uint16_t>(boneCount + view.identity.boneCount);
            materialCount = static_cast<std::uint16_t>(materialCount + view.identity.materialCount);
            ++modelCount;
            bool found = false;
            for (unsigned r = 0; r < rootCount; ++r) found |= roots[r] == root;
            if (!found) {
                if (rootCount == roots.size()) return false;
                roots[rootCount++] = root;
            }
        }
        return modelCount == recorded.header.modelCount && boneCount == recorded.header.boneCount &&
               materialCount == recorded.header.materialCount;
    }

    bool addLink(const void* link, const void* player, bool requireParent, bool includeFuse = false) {
        if (actorCount == kActorLimit) { error = Rejection::ActorLimit; return false; }
        auto& reference = references[actorCount];
        reference.emplace(mainBase_, link);
        const auto* actor = reference->get();
        if (!actor) { reference.reset(); return true; }
        for (std::uint16_t i = 0; i < actorCount; ++i) {
            if (actors[i] == actor) { reference.reset(); return true; }
        }
        if (requireParent) {
            const auto* components = registry(actor);
            const auto* controller = components ? read<const void*>(components, kModelController) : nullptr;
            using ParentLink = const void* (*)(const void*);
            const auto* parentLink = controller
                ? reinterpret_cast<ParentLink>(mainBase_ + kModelControllerGetParent)(controller) : nullptr;
            const auto matchesParent = [&](const void* candidate) {
                if (!candidate) return false;
                model::ScopedActorReference parent(mainBase_, candidate);
                return model::isEquipmentParent(parent.get(), player,
                                                 {actors.data(), actorCount});
            };
            if (!matchesParent(parentLink) && !model::hasBoundEquipmentParent(components,
                    [](const void* p, std::size_t offset) { return read<const void*>(p, offset); },
                    matchesParent)) {
                reference.reset();
                return true;
            }
        }
        actors[actorCount++] = actor;
        if (!appendModel(actor, false)) return false;
        if (includeFuse) {
            const auto* fusedLink = model::fusedEquipmentLink(registry(actor),
                [](const void* p, std::size_t offset) { return read<const void*>(p, offset); },
                [](const void* p, std::size_t offset) { return read<std::uint8_t>(p, offset); });
            if (fusedLink) {
                if (!addLink(fusedLink, player, false)) return false;
            }
        }
        return true;
    }

    bool belongsToQueue(const frame::CompletedModelPhase& phase) {
        const auto inspected = model::inspectCompletedQueue(phase.queue,
            {views.data(), modelCount}, bodyModels);
        error = inspected == model::CompletedQueueStatus::Limit
            ? Rejection::QueueLimit : Rejection::MissingQueue;
        return inspected == model::CompletedQueueStatus::Ready;
    }

    bool appendOwnedModels(const void* player) {
        const auto* components = registry(player);
        const auto* equipment = components ? read<const void*>(components, kEquipmentUser) : nullptr;
        if (!model::visitEquipmentLinks(equipment, [&](const void* link,
                model::EquipmentLinkKind kind, unsigned) {
            if (!addLink(link, player, true, kind == model::EquipmentLinkKind::Dynamic)) return false;
            return true;
        })) return false;
        const auto* component = components ? read<const void*>(components, kPlayerComponent) : nullptr;
        return !component || (addLink(at(component, 0x6B8), player, false) &&
                              addLink(at(component, 0x6D0), player, false));
    }
};

}

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

}

#include "RecallPlayback.hpp"

namespace self_recall::model {

enum class RenderInputStatus : std::uint8_t {
    Ready, IdentityChanged, OriginChanged, MissingBoneMetadata, RequiresLocalAnimation,
    MissingShapeMetadata, RequiresShapeAnimation,
};

struct NativeRenderInput {
    alignas(16) std::byte skeleton[0x48];
    std::byte skeletonPadding[8];
    alignas(16) std::byte model[0x90];
    NativeRenderInput() = default;
    NativeRenderInput(const NativeRenderInput&) = delete;
    NativeRenderInput& operator=(const NativeRenderInput&) = delete;

    RenderInputStatus prepare(const View& live, const pure::RecordedModelPose& historical,
                               const pure::RecordedBoneMatrix* bones) {
        const auto& a = live.identity;
        const auto& b = historical.identity;
        if (!bones || a.unit != b.unit || a.skeleton != b.skeleton || a.resource != b.resource ||
            a.boneCount != b.boneCount || a.materialCount != b.materialCount)
            return RenderInputStatus::IdentityChanged;
        if (!pure::sameRenderSpace(live.pose, historical))
            return RenderInputStatus::OriginChanged;
        const auto* originalSkeleton = reinterpret_cast<const std::byte*>(a.skeleton);
        const std::byte* metadata;
        std::memcpy(&metadata, originalSkeleton + 0x10, sizeof(metadata));
        if (!metadata) return RenderInputStatus::MissingBoneMetadata;
        for (unsigned i = 0; i < a.boneCount; ++i) {
            std::uint32_t flags;
            std::memcpy(&flags, metadata + i * 0x58 + 0x2C, sizeof(flags));
            if (flags & 0x70000u) return RenderInputStatus::RequiresLocalAnimation;
        }
        const auto* unit = reinterpret_cast<const std::byte*>(a.unit);
        std::uint16_t shapeCount;
        const std::byte* shapes;
        std::memcpy(&shapeCount, unit + 0x168, sizeof(shapeCount));
        std::memcpy(&shapes, unit + 0x178, sizeof(shapes));
        if (shapeCount && !shapes) return RenderInputStatus::MissingShapeMetadata;
        for (unsigned i = 0; i < shapeCount; ++i) {
            const std::byte* resource;
            std::memcpy(&resource, shapes + i * 0x70, sizeof(resource));
            if (!resource) return RenderInputStatus::MissingShapeMetadata;
            if (resource[0x5C] != std::byte{0}) return RenderInputStatus::RequiresShapeAnimation;
        }
        std::memcpy(skeleton, originalSkeleton, sizeof(skeleton));
        std::memcpy(skeleton + 0x20, &bones, sizeof(bones));
        std::memcpy(model, unit + 0x138, sizeof(model));
        const void* privateSkeleton = skeleton;
        std::memcpy(model + 0x38, &privateSkeleton, sizeof(privateSkeleton));
        return RenderInputStatus::Ready;
    }
};

struct NativeBoundingInput {
    alignas(16) std::byte unit[0x360];
    NativeBoundingInput() = default;
    NativeBoundingInput(const NativeBoundingInput&) = delete;
    NativeBoundingInput& operator=(const NativeBoundingInput&) = delete;

    void prepare(const void* liveUnit, const NativeRenderInput& animation) {
        std::memcpy(unit, liveUnit, sizeof(unit));
        const void* skeleton = animation.skeleton;
        std::memcpy(unit + 0x170, &skeleton, sizeof(skeleton));
    }

    void prepareHistorical(const void* liveUnit, const NativeRenderInput& animation,
                           const pure::RecordedModelPose& historical) {
        prepare(liveUnit, animation);
        std::memcpy(unit + 0x338, historical.renderOrigin, sizeof(historical.renderOrigin));
        const auto flags = std::to_integer<unsigned>(unit[0x355]);
        unit[0x355] = static_cast<std::byte>((flags & ~1u) | historical.originRelative);
    }
};

}

#include "RecallVisual.hpp"

namespace self_recall::model {

inline pure::AnimationVisibility nativeShapeVisibility(const void* nativeUnit, unsigned shapeIndex,
        const pure::RecordedPoseFrame& frame, unsigned modelIndex) {
    if (!nativeUnit || frame.header.modelCount > pure::kPoseModelLimit ||
        modelIndex >= frame.header.modelCount) return pure::AnimationVisibility::Invalid;
    const auto& expected = frame.models[modelIndex].identity;
    if (expected.unit != reinterpret_cast<std::uintptr_t>(nativeUnit))
        return pure::AnimationVisibility::Invalid;
    const auto read = []<class T>(const void* base, std::size_t offset, T& out) {
        std::memcpy(&out, static_cast<const std::byte*>(base) + offset, sizeof(T));
    };
    const void* skeleton;
    read(nativeUnit, 0x170, skeleton);
    if (!skeleton || expected.skeleton != reinterpret_cast<std::uintptr_t>(skeleton))
        return pure::AnimationVisibility::Invalid;
    const void* resource;
    read(skeleton, 0, resource);
    if (!resource || expected.resource != reinterpret_cast<std::uintptr_t>(resource))
        return pure::AnimationVisibility::Invalid;
    std::uint16_t bones, materials, shapes;
    read(resource, 0x38, bones);
    read(nativeUnit, 0x16A, materials);
    read(nativeUnit, 0x168, shapes);
    if (bones != expected.boneCount || materials != expected.materialCount || shapeIndex >= shapes)
        return pure::AnimationVisibility::Invalid;
    const std::byte* shapeArray;
    read(nativeUnit, 0x178, shapeArray);
    if (!shapeArray) return pure::AnimationVisibility::Invalid;
    const void* shape;
    read(shapeArray, shapeIndex * 0x70, shape);
    if (!shape) return pure::AnimationVisibility::Invalid;
    std::uint16_t material, bone;
    read(shape, 0x52, material);
    read(shape, 0x54, bone);
    return pure::animationShapeVisibility(frame, modelIndex, bone, material);
}

inline pure::AnimationVisibility nativeModelVisibility(const void* unit,
        const pure::RecordedPoseFrame& frame, unsigned modelIndex) {
    if (!unit || modelIndex >= frame.header.modelCount ||
        frame.header.modelCount > pure::kPoseModelLimit ||
        frame.models[modelIndex].identity.unit != reinterpret_cast<std::uintptr_t>(unit))
        return pure::AnimationVisibility::Invalid;
    std::uint16_t shapes;
    std::memcpy(&shapes, static_cast<const std::byte*>(unit) + 0x168, sizeof(shapes));
    bool visible = false;
    for (unsigned i = 0; i < shapes; ++i) {
        const auto shape = nativeShapeVisibility(unit, i, frame, modelIndex);
        if (shape == pure::AnimationVisibility::Invalid) return shape;
        visible |= shape == pure::AnimationVisibility::Visible;
    }
    return visible ? pure::AnimationVisibility::Visible : pure::AnimationVisibility::Hidden;
}

}

namespace self_recall::pose_recorder {

struct Control {
    std::uintptr_t scene = 0;
    std::uintptr_t player = 0;
    std::uint32_t worldGeneration = 0;
    std::uint8_t sampleFlags = 0;
    bool climbMayAdmit = false;
    bool vehicleMayAdmit = false;
    bool enabled = false;
};

void install(std::uintptr_t mainBase);
void publishControl(const Control& control);
void beginFrame(std::uint64_t epoch);
void modelsComplete(const frame::CompletedModelPhase& phase);
void prepareScene(void* scene, std::uint64_t epoch);

bool presentationPose(void* actor, pure::Pose& out);

bool trySuspend();
bool clearPending();
void resume(bool clearHistory);
void logDiagnostics();

}

namespace self_recall::pose_render {
bool copyBone(const void* unit, unsigned bone, float out[12]);

void install(std::uintptr_t mainBase);
void beginFrame(std::uint64_t epoch);
std::uint32_t latchedGeneration(std::uint64_t epoch);
enum class PaletteModel : unsigned { Foreign, Ready, Unavailable };
bool drawVisible(const void* renderUnit);
PaletteModel paletteModel(const void* unit, std::uint64_t epoch, std::uint32_t generation,
                          unsigned* failureDetail = nullptr);
bool copyWrist(std::uint32_t historyGeneration, pure::RenderWristFrame& out);
bool copyHeader(std::uint64_t epoch, std::uint32_t generation, pure::PoseFrameHeader& out);
pure::RenderFrameStore::Lease currentFrame(const void* body, std::uint64_t epoch);
void suppressEquipment(std::uint64_t epoch, std::span<const model::View> live);
void admitModels(void* scene, std::uint64_t epoch, std::span<const model::View> current,
                 std::span<const void* const> roots);
void verifyComplete(std::uint64_t epoch, const pure::RecordedPoseFrame& recorded,
             std::span<const model::View> current);
void collectorFailed(unsigned reason, unsigned detail);
void begin();
bool ready(std::uint32_t generation);
std::uint64_t takeFailure();
void reset();

}

namespace self_recall::pose_session {

struct BeginResult {
    bool pending = false;
    pure::PosePlaybackStatus status = pure::PosePlaybackStatus::NoHistory;
};

void initialize();
BeginResult begin(std::uint32_t world, const pure::GameTimeSnapshot& clock);
pure::PosePlayback* playback(); // Gameplay-thread access only.
bool publishSelected();
void reset(bool clearHistory = true);

pure::PoseReadLease acquireSelected(pure::PosePresentation* presentation = nullptr);
pure::PoseReadLease acquirePresentation(pure::PosePresentation* presentation = nullptr);
void latchPresentation(const pure::GameTimeSnapshot& clock);
bool active();
bool publishApplied(std::uintptr_t player, std::uint32_t actorId, std::uint32_t world,
                    const pure::HistorySample& sample);
bool appliedPose(std::uintptr_t player, std::uint32_t actorId, std::uint32_t world, pure::Pose& out);

}

namespace self_recall::pose_storage {

enum class State : std::uint8_t { WaitingForGameplay, Constructing, Ready };

void setPreparationAllowed(bool ready);
State prepare();

pure::PoseHistory* history();

}

#include "totk/engine/Raycast.hpp"

namespace self_recall::probe {

struct BatchPoll {
    totk::engine::RaycastPollStatus status = totk::engine::RaycastPollStatus::Idle;
    totk::engine::RaycastHit hit{};
    unsigned segment = 0;
};

class RouteProbeBatch {
public:
    bool begin(const pure::RouteProbePlan& plan, std::uint32_t mask,
               std::uint64_t tick, std::uint32_t generation) {
        if (pending_ || !plan.count || plan.count > boxes_.size()) return false;
        for (const auto& box : boxes_) if (box.busy()) return false;
        for (unsigned i = 0; i < plan.count; ++i) {
            totk::engine::RaycastRequest request{};
            request.from = plan.segments[i].from;
            request.to = plan.segments[i].to;
            request.relevancePoint = {(request.from.x + request.to.x) * 0.5f,
                                      (request.from.y + request.to.y) * 0.5f,
                                      (request.from.z + request.to.z) * 0.5f};
            request.mask = mask;
            request.submittedAt = {tick};
            request.generation = {generation};
            const auto begun = boxes_[i].begin(request);
            if (!begun) { cancel(); return false; }
            tickets_[i] = begun.value;
            segments_[i] = plan.segments[i];
            pending_ |= 1u << i;
        }
        return true;
    }

    BatchPoll poll(std::uint64_t tick, std::uint64_t timeout) {
        using Status = totk::engine::RaycastPollStatus;
        if (!pending_) return {};
        for (unsigned i = 0; i < boxes_.size(); ++i) {
            if (!(pending_ & (1u << i))) continue;
            const auto result = boxes_[i].poll(tickets_[i], {tick}, timeout);
            if (result.status == Status::Pending) continue;
            pending_ &= ~(1u << i);
            if (result.status == Status::Ready && result.result.hit &&
                pure::tolerableRouteContact(segments_[i], result.result.position,
                    {result.result.normal.x, result.result.normal.y, result.result.normal.z})) continue;
            if (result.status != Status::Ready || result.result.hit) {
                cancel();
                return {result.status, result.result, i};
            }
        }
        return {pending_ ? Status::Pending : Status::Ready, {}, 0};
    }

    void observe(totk::engine::RaycastFunction original, const void* from, const void* object) {
        for (auto& box : boxes_) box.observe(original, from, object);
    }
    void cancel() {
        for (auto& box : boxes_) if (box.busy()) box.cancel();
        pending_ = 0;
    }

private:
    std::array<totk::engine::RaycastMailbox, pure::kProbeWindowMaxSamples> boxes_{};
    std::array<totk::engine::RaycastTicket, pure::kProbeWindowMaxSamples> tickets_{};
    std::array<pure::RouteProbeSegment, pure::kProbeWindowMaxSamples> segments_{};
    unsigned pending_ = 0;
};
}

#include <cmath>

namespace self_recall::water {
inline bool surfaceHeight(const void* player, float& height) {
    using actor_model::read;
    const auto* registry = player ? actor_model::registry(player) : nullptr;
    const auto* physics = registry ? read<const void*>(registry, 0x50) : nullptr;
    const auto* controllers = physics ? read<const void*>(physics, 0x20) : nullptr;
    const auto* cct = controllers ? read<const void*>(controllers, 0x10) : nullptr;
    const auto* updates = cct ? read<const void*>(cct, 8) : nullptr;
    const auto* floating = updates ? read<const void*>(updates, 0x30) : nullptr;
    if (!floating || !(read<std::uint8_t>(floating, 0x19C) & 1) ||
        !read<std::uint8_t>(floating, 0x188)) return false;
    height = read<float>(floating, 0xE4);
    return std::isfinite(height);
}
}

#include "totk/core/Types.hpp"

namespace self_recall::probe {

constexpr int kTimeoutTicks = 24;
constexpr std::uint32_t kSolidMask = 0x20;

struct ProbeState {
    bool obstructed = false;
    bool unavailable = false;
    totk::core::WorldPosition hitPosition{};
    std::uint32_t timeouts = 0;
    pure::RouteProbePlan plan{};
    bool armed = false;
    std::uint32_t evaluatedThrough = 0;
    std::uint32_t requestedThrough = 0;
    std::uint32_t bypasses = 0;
};

struct ArmRequest {
    std::uint32_t rewindIndex = 0;
    bool rewinding = false;
    bool havePlayer = false;
    bool climbActive = false;
    std::uint64_t tick = 0;
    std::uint32_t generation = 0;
};

void arm(ProbeState& state, std::span<const pure::HistorySample> samples,
         const ArmRequest& request);

enum class ServiceResult : std::uint8_t { Quiet, TimedOut, Consumed };
ServiceResult service(ProbeState& state, bool rewinding, std::uint64_t tick);

void cancel(ProbeState& state);

void observe(totk::engine::RaycastFunction original, const void* from,
             const void* object);

}
