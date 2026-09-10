#pragma once
#include <array>
#include <optional>
#include "RecallActorModelView.hpp"
#include "RecallActorReference.hpp"
#include "RecallCompletedQueue.hpp"
#include "RecallEquipmentLinks.hpp"
#include "RecallEquipmentArchive.hpp"
#include "RecallOffsets121.hpp"
#include "RecallFrameHooks.hpp"

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
    std::uint16_t staticModels = 0;
    std::uint16_t parentSkipped = 0;
    std::uint16_t fusedLinks = 0, fusedModels = 0;
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
                ++parentSkipped;
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
                ++fusedLinks;
                const auto before = modelCount;
                if (!addLink(fusedLink, player, false)) return false;
                fusedModels = static_cast<std::uint16_t>(fusedModels + modelCount - before);
            }
        }
        return true;
    }

    bool belongsToQueue(const frame::CompletedModelPhase& phase) {
        const auto inspected = model::inspectCompletedQueue(phase.queue,
            {views.data(), modelCount}, bodyModels);
        error = inspected.status == model::CompletedQueueStatus::Limit
            ? Rejection::QueueLimit : Rejection::MissingQueue;
        return inspected.status == model::CompletedQueueStatus::Ready;
    }

    bool appendOwnedModels(const void* player) {
        const auto* components = registry(player);
        const auto* equipment = components ? read<const void*>(components, kEquipmentUser) : nullptr;
        if (!model::visitEquipmentLinks(equipment, [&](const void* link,
                model::EquipmentLinkKind kind, unsigned) {
            const auto before = modelCount;
            if (!addLink(link, player, true, kind == model::EquipmentLinkKind::Dynamic)) return false;
            if (kind == model::EquipmentLinkKind::Static)
                staticModels = static_cast<std::uint16_t>(staticModels + modelCount - before);
            return true;
        })) return false;
        const auto* component = components ? read<const void*>(components, kPlayerComponent) : nullptr;
        return !component || (addLink(at(component, 0x6B8), player, false) &&
                              addLink(at(component, 0x6D0), player, false));
    }
};

} // namespace self_recall::pose_recorder::detail
