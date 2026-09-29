#include "totk/engine/ReadGuard.hpp"
#include "RecallRuntimeEngine.hpp"
#include "GameProfiles.hpp"
#include "RecallModelEngine.hpp"
#include "RecallBase.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <optional>
#include <lib.hpp>
#include "FloatHook.hpp"

#include "RecallEffectsEngine.hpp"
#include "RecallRender.hpp"
#include "modules/self-recall/SelfRecallModule.hpp"
#include "totk/engine/Scene.hpp"
#include "totk/engine/Runtime.hpp"

namespace self_recall::frame {
namespace {

constexpr std::size_t kContextCompleted = 8;

struct FrameSites {
    std::uintptr_t frameStart, groupInvoke, singleInvoke, sceneCalc;
    std::uintptr_t frameRejoin, framePage, groupComplete, singleComplete;
    std::size_t sceneQueue, queueOwner, groupCount, singleCount;
    unsigned groupSceneRegister, singleSceneRegister;
    bool singleRegisterHoldsQueue;
};

constexpr std::array<FrameSites, 9> kFrameSites{{
    {0x93C3C4,0x94B248,0x94CAD8,0x93E068,0,0,0,0,0x42A0,0x40,0x58,0x20,0,0,false},
    {0x940F44,0x95012C,0x951B40,0x942790,0,0,0,0,0x42A0,0x40,0x58,0x20,0,0,false},
    {0x9120B4,0x9211F8,0x90F384,0x913900,0,0,0,0,0x42A0,0x40,0x58,0x20,0,0,false},
    {0x9111A8,0x91C6DC,0x90E818,0x912D14,0,0,0,0,0x42A0,0x40,0x58,0x20,0,0,false},
    {0x973550,0x981248,0x970820,0x974D9C,0,0,0,0,0x42A0,0x40,0x58,0x20,0,0,false},
    // 1.4.x single lane: last finisher of the single-root queue (+0x20/+0x28), the queue 1.2.1 hooks.
    {0,0,0,0x1B7C80,0x182064,0x3ABE000,0x16EA84,0x16E96C,0x3338,0x50,0x68,0x30,27,22,false},
    {0,0,0,0x195F80,0x1688B8,0x3AB9000,0x158860,0x15873C,0x3338,0x50,0x68,0x30,27,22,false},
    {0,0,0,0x108B50,0xDEB64,0x3ABB000,0xCB5BC,0xCB48C,0x3338,0x50,0x68,0x30,27,22,false},
    {0,0,0,0xBE5B0,0x8D7C8,0x3ACD000,0x79A80,0x79968,0x3338,0x50,0x68,0x30,28,22,false},
}};
const FrameSites* g_sites = nullptr;
std::uintptr_t g_frameMainBase = 0;
constexpr std::array<std::array<std::uint32_t, 4>, 9> kFrameWords{{
    {{0xD101C3FF,0x6DB923E9,0xFC190FE8,0xD101C3FF}},
    {{0xD101C3FF,0x6DB923E9,0xFC190FE8,0xA9BA7BFD}},
    {{0xD101C3FF,0x6DB923E9,0xD101C3FF,0xA9BA7BFD}},
    {{0xD101C3FF,0x6DB923E9,0xD101C3FF,0xA9BA7BFD}},
    {{0xD101C3FF,0x6DB923E9,0xD101C3FF,0xA9BA7BFD}},
    {{0x9001C9F7,0xD5033BBF,0xD5033BBF,0xD106C3FF}},
    {{0xB001CA97,0xD5033BBF,0xD5033BBF,0xD106C3FF}},
    {{0xB001CEF7,0xD5033BBF,0xD5033BBF,0xD106C3FF}},
    {{0x9001D217,0xD5033BBF,0xD5033BBF,0xD106C3FF}},
}};

Observers g_observers{};
std::atomic<std::uint64_t> g_epoch{0};
std::atomic<std::uint64_t> g_rejectedOwners{0};
std::atomic<std::uint64_t> g_joinFailures{0};
pure::ModelCompletionJoin<> g_join;
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

template <class T>
T read(const void* base, std::size_t offset) {
    if (!totk::engine::read_guard::admit(base, offset, sizeof(T))) return T{};
    T value;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

void beginFrame() {
    const auto epoch = g_epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    g_join.beginFrame(epoch);
    if (g_observers.beginFrame) g_observers.beginFrame(epoch);
}

HOOK_DEFINE_TRAMPOLINE(ModelFrameStartHook) {
    static void Callback(void* manager) {
        Orig(manager);
        beginFrame();
    }
};

HOOK_DEFINE_INLINE(ModelFrameRejoinHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        const auto sitePage = g_sites->frameRejoin & ~std::uintptr_t{0xFFF};
        const auto livePage = (g_frameMainBase + g_sites->frameRejoin) & ~std::uintptr_t{0xFFF};
        ctx->X[23] = livePage + (g_sites->framePage - sitePage);
        if (ctx->X[20]) beginFrame();
    }
};

void queueComplete(void* queue, void* context, pure::ModelQueueLane lane) {
    if (!g_sites || !queue || (context && !read<std::uint8_t>(context, kContextCompleted))) return;

    std::atomic_thread_fence(std::memory_order_acquire);
    auto* scene = read<void*>(queue, g_sites->queueOwner);
    if (!scene || reinterpret_cast<std::uintptr_t>(scene) + g_sites->sceneQueue !=
                      reinterpret_cast<std::uintptr_t>(queue)) {
        const auto count = g_rejectedOwners.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 4 || count % 1800 == 0)
            Logging.Log("[self-recall] model queue owner rejected total=%llu",
                static_cast<unsigned long long>(count));
        return;
    }
    const auto epoch = g_epoch.load(std::memory_order_acquire);
    if (!epoch) return;
    const auto joined = g_join.complete(reinterpret_cast<std::uintptr_t>(queue), epoch, lane);
    if (joined != pure::ModelJoinStatus::Complete) {
        if (joined != pure::ModelJoinStatus::Waiting) {
            const auto count = g_joinFailures.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count <= 4 || count % 1800 == 0)
                Logging.Log("[self-recall] model join rejected: status=%u lane=%u epoch=%llu total=%llu",
                    static_cast<unsigned>(joined), static_cast<unsigned>(lane),
                    static_cast<unsigned long long>(epoch), static_cast<unsigned long long>(count));
        }
        return;
    }
    if (g_observers.modelsComplete) {
        const CompletedModelPhase phase{scene, queue, epoch,
                                         read<std::uint32_t>(queue, g_sites->groupCount),
                                         read<std::uint32_t>(queue, g_sites->singleCount)};
        g_observers.modelsComplete(phase);
    }
}

HOOK_DEFINE_TRAMPOLINE(ModelSingleQueueHook) {
    static std::uintptr_t Callback(void* queue, void* context) {
        const auto result = Orig(queue, context);
        queueComplete(queue, context, pure::ModelQueueLane::Single);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(ModelCalcQueueHook) {
    static void Callback(void* queue, void* context) {
        Orig(queue, context);
        queueComplete(queue, context, pure::ModelQueueLane::Multi);
    }
};

HOOK_DEFINE_INLINE(ModelSingleCompleteHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        auto* sceneOrQueue = reinterpret_cast<std::byte*>(ctx->X[g_sites->singleSceneRegister]);
        if (!sceneOrQueue) return;
        auto* queue = g_sites->singleRegisterHoldsQueue
            ? sceneOrQueue : sceneOrQueue + g_sites->sceneQueue;
        queueComplete(queue, nullptr, pure::ModelQueueLane::Single);
    }
};

HOOK_DEFINE_INLINE(ModelGroupCompleteHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        auto* scene = reinterpret_cast<std::byte*>(ctx->X[g_sites->groupSceneRegister]);
        if (scene)
            queueComplete(scene + g_sites->sceneQueue, nullptr, pure::ModelQueueLane::Multi);
    }
};

HOOK_DEFINE_TRAMPOLINE(SceneCalcFrameHook) {
    static std::uintptr_t Callback(void* scene) {
        const auto epoch = g_epoch.load(std::memory_order_acquire);
        if (epoch && !g_join.registerScene(reinterpret_cast<std::uintptr_t>(scene) + g_sites->sceneQueue, epoch)) {
            const auto count = g_joinFailures.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count <= 4 || count % 1800 == 0)
                Logging.Log("[self-recall] model join scene limit: epoch=%llu total=%llu",
                    static_cast<unsigned long long>(epoch), static_cast<unsigned long long>(count));
        }
        if (epoch && g_observers.prepareScene) g_observers.prepareScene(scene, epoch);
        return Orig(scene);
    }
};

}

bool sitesValid(std::uintptr_t mainBase, std::size_t textSize) {
    const auto& sites = profiles::row(kFrameSites);
    const std::array<std::uintptr_t, 4> offsets{
        sites.frameStart ? sites.frameStart : sites.frameRejoin,
        sites.groupInvoke ? sites.groupInvoke : sites.groupComplete,
        sites.singleInvoke ? sites.singleInvoke : sites.singleComplete,
        sites.sceneCalc};
    return profiles::holds(mainBase, textSize, offsets, profiles::row(kFrameWords));
}

void install(Observers observers) {
    g_observers = observers;
    g_sites = &profiles::row(kFrameSites);
    g_frameMainBase = exl::util::modules::GetTargetStart();
    if (g_sites->frameStart) {
        ModelFrameStartHook::InstallAtOffset(g_sites->frameStart);
        ModelCalcQueueHook::InstallAtOffset(g_sites->groupInvoke);
        ModelSingleQueueHook::InstallAtOffset(g_sites->singleInvoke);
    } else {
        ModelFrameRejoinHook::InstallAtOffset(g_sites->frameRejoin);
        ModelGroupCompleteHook::InstallAtOffset(g_sites->groupComplete);
        ModelSingleCompleteHook::InstallAtOffset(g_sites->singleComplete);
    }
    SceneCalcFrameHook::InstallAtOffset(g_sites->sceneCalc);
}

std::uint64_t epoch() { return g_epoch.load(std::memory_order_acquire); }

}

namespace self_recall::pose_recorder {
using namespace actor_model;
using namespace detail;
using namespace offsets121::pose_recorder;
namespace {

std::uintptr_t g_mainBase = 0;
struct ModelFrameTime {
    std::uint64_t epoch = 0;
    pure::GameTimeSnapshot time{};
};
pure::FrameMailbox<ModelFrameTime> g_modelTime;
pure::FrameMailbox<Control> g_control;
pure::FrameMailbox<pure::ActorFrameTicket> g_actorTicket;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_suspended{false};
std::atomic<bool> g_clearRequested{false};
std::atomic<std::uintptr_t> g_player{0};
std::atomic_flag g_collecting = ATOMIC_FLAG_INIT;
model::CaptureWorkspace g_workspace{};
std::uint64_t g_lastEpoch = 0;
std::uint64_t g_lastTimeSerial = 0;
std::atomic<std::uint64_t> g_rejections{0};
std::uint32_t g_world = 0;
// 1.4.3's inlined matrix update ends at this store with the actor in X19.
constexpr std::array<std::uintptr_t, 9> kFoldedMatrixSites{0, 0, 0, 0, 0, 0, 0, 0, 0x24720C};
constexpr std::uint32_t kFoldedMatrixWord = 0xB905AE68;
std::uintptr_t g_foldedMatrixSite = 0;

bool appliedPlayerPose(void* actor, pure::Pose& applied) {
    const auto player = reinterpret_cast<std::uintptr_t>(actor);
    Control appliedControl;
    if (player == g_player.load(std::memory_order_acquire) &&
        g_control.snapshot(appliedControl) && appliedControl.player == player &&
        pose_session::appliedPose(player, read<std::uint32_t>(actor, kActorId),
                                  appliedControl.worldGeneration, applied)) {
        return true;
    }

    return false;
}

HOOK_DEFINE_TRAMPOLINE(ControllerMatrixHook) {
    static u64 Callback(void* controller, float* matrix, float* linear, float* angular,
                        float* previousLinear, float* previousAngular) {
        const auto result = Orig(controller, matrix, linear, angular, previousLinear, previousAngular);
        const auto player = g_player.load(std::memory_order_acquire);
        const pure::ControllerPoseOutput output{matrix, {linear, angular, previousLinear, previousAngular}};
        if (!(result & 1u) ||
            !output.playerCommit(player, totk::engine::layout::kActorLinearVelocity)) return result;
        auto* actor = reinterpret_cast<void*>(player);
        pure::Pose applied;
        if (!appliedPlayerPose(actor, applied)) return result;
        const auto* registry = read<const void*>(actor, totk::engine::layout::kActorComponentRegistry);
        const auto* physics = registry
                                  ? read<const void*>(
                                        registry,
                                        totk::engine::layout::kPhysicsFromRegistry)
                                  : nullptr;
        if (!physics || read<const void*>(physics, totk::engine::layout::kRigidBodySetFromPhysics) != controller)
            return result;
        (void)output.apply(applied);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(PositionRotationHook) {
    static u64 Callback(void* actor, float* matrix, float frameScale) {
        pure::Pose applied;
        // A folded updateMatrix skips PlayerMatrixHook, so detach steering before the model update here.
        if (g_foldedMatrixSite && appliedPlayerPose(actor, applied))
            (void)vehicle::detachControlStick(g_mainBase, actor);
        const auto result = Orig(actor, matrix, frameScale);
        if (!(result & 1u) || !actor || !matrix || !appliedPlayerPose(actor, applied))
            return result;
        auto* bytes = static_cast<std::byte*>(actor);
        auto* velocity = reinterpret_cast<float*>(
            bytes + totk::engine::layout::kActorLinearVelocity);
        const pure::ControllerPoseOutput output{
            matrix, {velocity, velocity + 3, velocity + 6, velocity + 9}};
        if (!output.apply(applied)) return result;
        std::memcpy(bytes + totk::engine::layout::kActorPosition,
                    &applied.position, sizeof(applied.position));
        std::memcpy(bytes + totk::engine::layout::kActorRotation,
                    &applied.rotation, sizeof(applied.rotation));
        return result;
    }
};

void recordCompletedPlayerMatrix(void* actor) {
    if (!g_enabled.load(std::memory_order_acquire) ||
        reinterpret_cast<std::uintptr_t>(actor) != g_player.load(std::memory_order_acquire))
        return;
    Control control;
    if (!g_control.snapshot(control) || !control.enabled ||
        control.player != reinterpret_cast<std::uintptr_t>(actor)) return;
    const auto* model = actorModel(actor);
    if (!model) return;
    pure::ActorFrameTicket ticket{};
    ticket.actor = control.player;
    ticket.actorId = read<std::uint32_t>(actor, kActorId);
    ticket.model = reinterpret_cast<std::uintptr_t>(model);
    ticket.worldGeneration = control.worldGeneration;
    ticket.route.pose = actorPose(actor);
    ticket.haveWaterHeight = water::surfaceHeight(actor, ticket.waterHeight);
    if (!pure::finitePose(ticket.route.pose)) return;
    std::memcpy(ticket.modelRoot, at(model, kModelRoot), sizeof(ticket.modelRoot));
    std::memcpy(ticket.route.engineVelocity,
                at(actor, totk::engine::layout::kActorLinearVelocity),
                sizeof(ticket.route.engineVelocity));
    ticket.route.flags = control.sampleFlags;
    ticket.route.flags = pure::pairNativeClimbAdmission(ticket.route.flags,
        control.climbMayAdmit, glider_release::nativeClimbing(ticket.actorId));
    if (glider_release::nativeGliding(ticket.actorId)) ticket.route.flags |= pure::SampleNativeGlide;
    ticket.route.flags = pure::pairVehicleAdmission(ticket.route.flags, control.vehicleMayAdmit,
        vehicle::controlStickActive(g_mainBase, actor));
    (void)g_actorTicket.publish(ticket);
}

HOOK_DEFINE_TRAMPOLINE(PlayerMatrixHook) {
    static u64 Callback(void* actor, void* matrixOutput, const void* calculation) {
        pure::Pose applied;
        if (appliedPlayerPose(actor, applied))
            (void)vehicle::detachControlStick(g_mainBase, actor);
        const auto result = Orig(actor, matrixOutput, calculation);
        recordCompletedPlayerMatrix(actor);
        return result;
    }
};

HOOK_DEFINE_INLINE(FoldedPlayerMatrixHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) {
        auto* gp = integerRegisters(ctx);
        recordCompletedPlayerMatrix(reinterpret_cast<void*>(gp->X[19]));
    }
};

void reject(Rejection reason, std::uint64_t epoch, std::uint32_t detail = 0) {
    if (pose_session::active()) {
        pose_render::collectorFailed(static_cast<unsigned>(reason), detail);
    } else if (auto* history = pose_storage::history(); history && history->count()) {
        history->clear();
    }
    const auto count = g_rejections.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 8 || (count % 300) == 0)
        Logging.Log("[self-recall] pose snapshot rejected: reason=%u detail=%u epoch=%llu total=%llu",
                    static_cast<unsigned>(reason), detail,
                    static_cast<unsigned long long>(epoch),
                    static_cast<unsigned long long>(count));
}

std::atomic<std::uint64_t> g_modelLimitLogs{0};

void rejectCollection(const OwnedModelCollection& collection, std::uint64_t epoch) {
    const auto& hit = collection.limitHit;
    if (collection.error == Rejection::ModelLimit && hit.site != OwnedModelCollection::LimitSite::None) {
        const auto count = g_modelLimitLogs.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 16 || (count % 300) == 0) {
            Logging.Log("[self-recall] MODEL_LIMIT site=%u actor_models=%d models=%u/%u "
                        "bones=%u+%u/%u materials=%u+%u/%u epoch=%llu total=%llu",
                        static_cast<unsigned>(hit.site), hit.actorModels,
                        hit.models, pure::kPoseModelLimit, hit.bones, hit.addBones, pure::kPoseBoneLimit,
                        hit.materials, hit.addMaterials, pure::kPoseMaterialLimit,
                        static_cast<unsigned long long>(epoch), static_cast<unsigned long long>(count));
        }
    }
    reject(collection.error, epoch);
}

bool pairCompletedActorPose(const Control& control, const void* player,
                            pure::PoseHistory* history, std::uint64_t epoch,
                            pure::ActorFrameTicket& ticket) {
    if (g_clearRequested.exchange(false, std::memory_order_acq_rel)) history->clear();
    if (g_world != control.worldGeneration) {
        history->clear();
        g_world = control.worldGeneration;
    }
    const auto* root = actorModel(player);
    if (!root) { reject(Rejection::MissingRoot, epoch); return false; }
    const bool haveTicket = g_actorTicket.snapshot(ticket);
    const auto currentActorId = read<std::uint32_t>(player, kActorId);
    const auto currentPose = actorPose(player);
    const auto* currentRoot = static_cast<const float*>(at(root, kModelRoot));
    if (!haveTicket || !pure::matchesActorFrame(ticket, control.player, currentActorId,
                              reinterpret_cast<std::uintptr_t>(root), control.worldGeneration,
                              currentPose, currentRoot)) {
        const unsigned mismatch = (!haveTicket ? 64u : 0u) |
        (ticket.actor != control.player ? 1u : 0u) |
        (ticket.actorId != currentActorId ? 2u : 0u) |
        (ticket.model != reinterpret_cast<std::uintptr_t>(root) ? 4u : 0u) |
        (ticket.worldGeneration != control.worldGeneration ? 8u : 0u) |
        (std::memcmp(&ticket.route.pose, &currentPose, sizeof(currentPose)) ? 16u : 0u) |
        (std::memcmp(ticket.modelRoot, currentRoot, sizeof(ticket.modelRoot)) ? 32u : 0u);
        reject(Rejection::UnpairedRoot, epoch, mismatch); return false;
    }
    return true;
}

void recordOwnedFrame(const frame::CompletedModelPhase& phase, const Control& control,
                      const ModelFrameTime& frameTime, const void* player,
                      OwnedModelCollection& collection, const pure::ActorFrameTicket& ticket,
                      pure::PoseHistory* history) {
    equipment::beginRecord(history->generation());
    equipment::collectExpired(*history, control.worldGeneration);
    if (!collection.archiveEquipment(player, control.worldGeneration)) {
        rejectCollection(collection, phase.epoch); return;
    }
    pure::PoseFrameHeader header{};
    header.frameEpoch = phase.epoch;
    header.elapsedNanoseconds = frameTime.time.elapsedNanoseconds;
    header.worldGeneration = control.worldGeneration;
    header.modelGeneration = 1;
    header.bodyModelCount = static_cast<std::uint8_t>(collection.bodyModels);
    header.route = ticket.route;
    header.haveWaterHeight = ticket.haveWaterHeight;
    header.waterHeight = ticket.waterHeight;
    if (const auto previous = history->newest(); previous) {
        const auto& prior = previous.get()->header;
        if (prior.worldGeneration == header.worldGeneration &&
            header.elapsedNanoseconds > prior.elapsedNanoseconds) {
            const auto seconds = static_cast<double>(header.elapsedNanoseconds -
                                                      prior.elapsedNanoseconds) / 1.0e9;
            header.route.pathSpeed = static_cast<float>(pure::distance3(
                header.route.pose.position, prior.route.pose.position) / seconds);
        }
    }
    header.modelCount = collection.modelCount;
    header.boneCount = collection.boneCount;
    header.materialCount = collection.materialCount;
#if SELF_RECALL_STORAGE_PROFILE == 7
    collection.encodeGearIdentities();
#endif
    equipment::recordEffects(header, {collection.views.data(), collection.modelCount});
    const auto result = model::recordCompleted(header,
        {collection.views.data(), header.modelCount}, g_workspace, *history);
    if (result.status != model::CaptureStatus::Recorded) {
        reject(Rejection::CaptureRejected, phase.epoch,
               static_cast<unsigned>(result.status) * 16u + static_cast<unsigned>(result.history.status));
        return;
    }
    g_lastEpoch = phase.epoch;
    g_lastTimeSerial = frameTime.time.serial;
    if (auto latest = history->acquire(result.history.key); latest) {
        if (!equipment::recorded(*latest.get())) {
            reject(Rejection::EquipmentArchive, phase.epoch, 0xA001);
            return;
        }
    }
    history->trimToWindow(pure::kRecallWindowNanoseconds);
}

}

bool presentationPose(void* actor, pure::Pose& out) {
    return appliedPlayerPose(actor, out);
}

void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    equipment::install(mainBase);
    if constexpr (!pure::kHistoricalEquipment) equipment_effects::install(mainBase);
    PlayerMatrixHook::InstallAtOffset(profiles::address(kActorUpdateMatrix));
    g_foldedMatrixSite = profiles::row(kFoldedMatrixSites);
    if (g_foldedMatrixSite) FoldedPlayerMatrixHook::InstallAtOffset(g_foldedMatrixSite);
    if (profiles::newerRenderer())
        PositionRotationHook::InstallAtOffset(profiles::address(kControllerMatrixAndVelocity));
    else
        ControllerMatrixHook::InstallAtOffset(profiles::address(kControllerMatrixAndVelocity));
}

bool sitesValid(std::uintptr_t mainBase, std::size_t textSize) {
    const auto site = profiles::row(kFoldedMatrixSites);
    return !site || profiles::holds(mainBase, textSize, site, kFoldedMatrixWord);
}

void publishControl(const Control& control) {
    g_enabled.store(false, std::memory_order_release);
    g_player.store(control.player, std::memory_order_release);
    if (g_control.publish(control))
        g_enabled.store(control.enabled, std::memory_order_release);
}

void beginFrame(std::uint64_t epoch) {
    ModelFrameTime value{epoch, {}};
    (void)game_clock::snapshot(value.time);
    (void)g_modelTime.publish(value);
}

bool prepareCurrentEquipment(pure::RecordedPoseFrame& frame,
                             const pure::RecordedPoseFrame& recorded, std::uint64_t& report) {
    report = 0xFF00000000000000ull;
    Control control;
    if (!g_control.snapshot(control) || !control.worldGeneration ||
        control.worldGeneration != frame.header.worldGeneration) return false;
    const auto scene = totk::engine::resolveScene(g_mainBase);
    if (!scene || scene.value.token.value != control.scene) return false;
    using PlayerLink = const void* (*)(std::uintptr_t);
    const auto* link = reinterpret_cast<PlayerLink>(g_mainBase + profiles::address(kResidentPlayerLink))(
        scene.value.residentActorManager);
    if (!link) return false;
    OwnedModelCollection collection(g_mainBase);
    collection.references[0].emplace(g_mainBase, link);
    const auto* player = collection.references[0]->get();
    if (!player || reinterpret_cast<std::uintptr_t>(player) != control.player) return false;
    collection.actors[0] = player;
    collection.actorCount = 1;
    if (!collection.appendModel(player, true) || !collection.appendOwnedModels(player, true)) {
        report |= unsigned(collection.error); return false;
    }
    unsigned detail = 0, matched = 0, inherited = 0;
#if SELF_RECALL_STORAGE_PROFILE == 7
    const auto& lastBody = frame.models[frame.header.bodyModelCount - 1].identity;
    frame.header.modelCount = frame.header.bodyModelCount;
    frame.header.boneCount = lastBody.firstBone + lastBody.boneCount;
    frame.header.materialCount = lastBody.firstMaterial + lastBody.materialCount;
    const auto clothingModels = collection.clothingModels;
#else
    const auto clothingModels = collection.modelCount;
    (void)recorded;
#endif
    const auto status = model::appendCurrentClothing(frame,
        {collection.views.data(), clothingModels}, model::boneName, model::boneParent,
        detail, matched, inherited);
    report = (std::uint64_t{unsigned(status)} << 56) | (std::uint64_t{frame.header.modelCount} << 48) |
        (status == model::ClothingPoseStatus::Ready
            ? (std::uint64_t{matched} << 32) | (std::uint64_t{inherited} << 16) : detail);
    if (status != model::ClothingPoseStatus::Ready) return false;
#if SELF_RECALL_STORAGE_PROFILE == 7
    collection.resolveGearParents(player);
    unsigned gearModels = 0, fallbackModels = 0, unresolvedParents = 0;
    if (!model::appendCurrentGear(frame, recorded,
            {collection.views.data(), collection.modelCount},
            {collection.gearBindings.data(), collection.modelCount},
            clothingModels, detail, gearModels, fallbackModels, &unresolvedParents)) {
        report = 0x0800000000000000ull | detail; return false;
    }
    report = (report & 0x0000FFFFFFFF0000ull) | (std::uint64_t{frame.header.modelCount} << 48) |
             (gearModels << 8) | fallbackModels | (unresolvedParents ? 0x80u : 0u);
#endif
    return true;
}

void prepareScene(void* nativeScene, std::uint64_t epoch) {
    if (!pose_session::active()) return;
    Control control;
    if (!g_control.snapshot(control) || !control.worldGeneration) return;
    const auto scene = totk::engine::resolveScene(g_mainBase);
    if (!scene || scene.value.token.value != control.scene) return;
    using PlayerLink = const void* (*)(std::uintptr_t);
    const auto* link = reinterpret_cast<PlayerLink>(g_mainBase + profiles::address(kResidentPlayerLink))(
        scene.value.residentActorManager);
    if (!link) return;
    OwnedModelCollection collection(g_mainBase);
    collection.references[0].emplace(g_mainBase, link);
    const auto* player = collection.references[0]->get();
    if (!player || reinterpret_cast<std::uintptr_t>(player) != control.player) return;
    const auto* root = actorModel(player);
    if (!root || read<const void*>(root, 0x60) != nativeScene) return;
    collection.actors[0] = player;
    collection.actorCount = 1;
    if (!collection.appendModel(player, true) || !collection.appendOwnedModels(player, true)) {
        rejectCollection(collection, epoch);
        return;
    }
    auto historical = pose_render::currentFrame(
        reinterpret_cast<const void*>(collection.views[0].identity.unit), epoch);
    if (!historical) return;
    equipment_effects::publishLive({collection.actors.data() + 1, collection.actorCount - 1u});
    if constexpr (pure::kHistoricalEquipment)
        pose_render::suppressEquipment(epoch, {collection.views.data() + collection.bodyModels,
                                   static_cast<std::size_t>(collection.modelCount - collection.bodyModels)});
    if (!collection.useHistoricalEquipment(player, nativeScene, historical.get()->animation)) {
        rejectCollection(collection, epoch);
        return;
    }
    Control latest;
    if (!g_control.snapshot(latest) || latest.player != control.player || latest.scene != control.scene ||
        latest.worldGeneration != control.worldGeneration) {
        reject(Rejection::ControlChanged, epoch);
        return;
    }
    pose_render::admitModels(nativeScene, epoch, {collection.views.data(), collection.modelCount},
                            {collection.roots.data(), collection.rootCount});
}

bool trySuspend() {
    g_suspended.store(true, std::memory_order_release);
    if (g_collecting.test_and_set(std::memory_order_acquire)) return false;
    g_collecting.clear(std::memory_order_release);
    return true;
}

bool clearPending() { return g_clearRequested.load(std::memory_order_acquire); }

void resume(bool clearHistory) {
    if (clearHistory) g_clearRequested.store(true, std::memory_order_release);
    g_suspended.store(false, std::memory_order_release);
}

void modelsComplete(const frame::CompletedModelPhase& phase) {
    const bool rendering = pose_session::active();
    if (!rendering && (!g_enabled.load(std::memory_order_acquire) ||
                       g_suspended.load(std::memory_order_acquire))) return;
    auto* history = pose_storage::history();
    if (!history || g_collecting.test_and_set(std::memory_order_acquire)) return;
    struct Unlock { ~Unlock() { g_collecting.clear(std::memory_order_release); } } unlock;
    if (!rendering && g_suspended.load(std::memory_order_acquire)) return;
    ModelFrameTime frameTime{};
    if (!g_modelTime.snapshot(frameTime) || frameTime.epoch != phase.epoch) return;
    if (!rendering && (frameTime.time.status != pure::GameTimeStatus::Running ||
                       frameTime.time.serial == g_lastTimeSerial)) return;
    Control control{};
    if (!g_control.snapshot(control) || (!rendering && !control.enabled) || !control.worldGeneration) return;
    const auto scene = totk::engine::resolveScene(g_mainBase);
    if (!scene || scene.value.token.value != control.scene) return;
    using PlayerLink = const void* (*)(std::uintptr_t);
    const auto* link = reinterpret_cast<PlayerLink>(g_mainBase + profiles::address(kResidentPlayerLink))(
        scene.value.residentActorManager);
    if (!link) { reject(Rejection::MissingPlayer, phase.epoch); return; }
    OwnedModelCollection collection(g_mainBase);
    collection.references[0].emplace(g_mainBase, link);
    const auto* player = collection.references[0]->get();
    if (!player || reinterpret_cast<std::uintptr_t>(player) != control.player) {
        reject(Rejection::MissingPlayer, phase.epoch); return;
    }
    const auto* bodyRoot = actorModel(player);
    if (!bodyRoot) { reject(Rejection::MissingRoot, phase.epoch); return; }
    if (read<const void*>(bodyRoot, 0x60) != phase.scene) return;
    collection.actors[0] = player;
    collection.actorCount = 1;
    if (!collection.appendModel(player, true)) { rejectCollection(collection, phase.epoch); return; }
    if (!rendering && phase.epoch == g_lastEpoch) return;
    pure::ActorFrameTicket ticket{};
    if (!rendering && !pairCompletedActorPose(control, player, history, phase.epoch, ticket)) return;
    if (!collection.appendOwnedModels(player, rendering)) {
        rejectCollection(collection, phase.epoch); return;
    }
    auto historical = rendering ? pose_render::currentFrame(
        reinterpret_cast<const void*>(collection.views[0].identity.unit), phase.epoch)
        : pure::RenderFrameStore::Lease{};
    if (rendering) {
        if (!historical) return;
        if (!collection.useHistoricalEquipment(player, phase.scene, historical.get()->animation)) {
            rejectCollection(collection, phase.epoch); return;
        }
    }
    if (!collection.belongsToQueue(phase)) {
        reject(collection.error, phase.epoch, (phase.queuedSingles << 16) | phase.queuedGroups); return;
    }
    Control current;
    if (!g_control.snapshot(current) || current.scene != control.scene || current.player != control.player ||
        current.worldGeneration != control.worldGeneration) {
        reject(Rejection::ControlChanged, phase.epoch); return;
    }
    if (rendering) {
        if (historical.get()->animation.header.worldGeneration == control.worldGeneration)
            pose_render::verifyComplete(phase.epoch, historical.get()->animation,
                                  {collection.views.data(), collection.modelCount});
        return;
    }
    if (!g_enabled.load(std::memory_order_acquire) || !current.enabled ||
        g_suspended.load(std::memory_order_acquire)) return;
    recordOwnedFrame(phase, control, frameTime, player, collection, ticket, history);
}


}
