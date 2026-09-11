#include "RecallOffsets121.hpp"
#include "RecallMemoryProfiler.hpp"
#include "RecallCorpusCapture.hpp"
#include "RecallActorModelView.hpp"
#include "RecallModelCollection.hpp"
#include "RecallPoseRecorder.hpp"
#include "RecallGliderRelease.hpp"
#include "RecallVehicle.hpp"
#include "RecallVehiclePolicy.hpp"
#include "RecallWater.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <optional>
#include <lib.hpp>

#include "RecallActorReference.hpp"
#include "RecallCompletedQueue.hpp"
#include "RecallEquipmentLinks.hpp"
#include "RecallEquipmentArchive.hpp"
#include "RecallEquipmentEffects.hpp"
#include "modules/self-recall/RecallAnimation.hpp"
#include "RecallFrameTicket.hpp"
#include "RecallControllerPose.hpp"
#include "RecallGameClock.hpp"
#include "RecallModelView.hpp"
#include "RecallPoseStorage.hpp"
#include "RecallPoseSession.hpp"
#include "RecallPoseRender.hpp"
#include "totk/engine/Scene.hpp"
#include "totk/engine/Totk121Offsets.hpp"

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
std::atomic<std::uint64_t> g_rejections{0}, g_recorded{0};
std::uint32_t g_world = 0;

enum class Gate : unsigned {
    Disabled, Suspended, Storage, Busy, FrameTime, Clock, Control, Scene,
    OtherModelScene, Duplicate, FinalControl, Count,
};
constexpr const char* kGateNames[]{"disabled", "suspended", "storage", "busy", "frame_time",
    "clock", "control", "scene", "other_model_scene", "duplicate", "final_control"};
struct GateCounter { std::atomic<std::uint64_t> count{0}, detail{0}, epoch{0}; };
std::array<GateCounter, static_cast<unsigned>(Gate::Count)> g_gates{};
void skipped(Gate gate, std::uint64_t epoch, std::uint64_t detail = 0) {
    auto& counter = g_gates[static_cast<unsigned>(gate)];
    counter.detail.store(detail, std::memory_order_relaxed);
    counter.epoch.store(epoch, std::memory_order_relaxed);
    counter.count.fetch_add(1, std::memory_order_relaxed);
}

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
        if (!(result & 1u) || !output.playerCommit(player)) return result;
        auto* actor = reinterpret_cast<void*>(player);
        pure::Pose applied;
        if (!appliedPlayerPose(actor, applied)) return result;
        const auto* registry = read<const void*>(actor, totk::engine::layout::kActorComponentRegistry);
        const auto* physics = registry ? read<const void*>(registry, totk::engine::layout::kPhysicsFromRegistry) : nullptr;
        if (!physics || read<const void*>(physics, totk::engine::layout::kRigidBodySetFromPhysics) != controller)
            return result;
        const totk::core::WorldPosition nativePosition{matrix[3], matrix[7], matrix[11]};
        const float correction = pure::distance3(nativePosition, applied.position);
        if (!output.apply(applied)) return result;
        static std::atomic<std::uint64_t> corrections{0};
        const auto n = corrections.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 3 || n % 300 == 0)
            Logging.Log("[self-recall] PLAYER_CONTROLLER_POSE count=%llu correction_cm=%d",
                static_cast<unsigned long long>(n), std::isfinite(correction) ? int(correction * 100) : -1);
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
    ticket.route.recordedTick = control.tick;
    ticket.route.tickDelta = 1;
    ticket.route.flags = control.sampleFlags;
    ticket.route.flags = pure::pairNativeClimbAdmission(ticket.route.flags,
        control.climbMayAdmit, glider_release::nativeClimbing(ticket.actorId));
    if (glider_release::nativeGliding(ticket.actorId)) ticket.route.flags |= pure::SampleNativeGlide;
    ticket.route.flags = pure::pairVehicleAdmission(ticket.route.flags, control.vehicleMayAdmit,
        vehicle::controlStickActive(g_mainBase, actor));
    ticket.route.stickX = control.stickX;
    ticket.route.stickY = control.stickY;
    const auto animation = anim::capture(actor);
    ticket.route.animKind = animation.kind;
    ticket.route.animSlot = animation.slot;
    ticket.route.animFrame = animation.frame;
    ticket.route.animRate = animation.rate;
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
        reject(collection.error, phase.epoch); return;
    }
    pure::PoseFrameHeader header{};
    header.frameEpoch = phase.epoch;
    header.elapsedNanoseconds = frameTime.time.elapsedNanoseconds;
    header.worldGeneration = control.worldGeneration;
    header.modelGeneration = 1; // Body identity stays strict; equipment has independent owners.
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
    equipment::recordEffects(header, {collection.views.data(), collection.modelCount});
#if SELF_RECALL_MEMORY_PROFILE
    const auto captureStart = svcGetSystemTick();
#endif
    const auto result = model::recordCompleted(header,
        {collection.views.data(), collection.modelCount}, g_workspace, *history);
#if SELF_RECALL_MEMORY_PROFILE
    memory_profile::recordCaptureTicks(svcGetSystemTick() - captureStart);
#endif
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
            memory_profile::recordPose(*history, result.history.key);
            return;
        }
        corpus::pose(*latest.get());
    }
    history->trimToWindow(pure::kRecallWindowNanoseconds);
    memory_profile::recordPose(*history, result.history.key);
    const auto recorded = g_recorded.fetch_add(1, std::memory_order_relaxed) + 1;
    static std::uint16_t lastFusedLinks = 0, lastFusedModels = 0;
    const bool fusedChanged = collection.fusedLinks != lastFusedLinks || collection.fusedModels != lastFusedModels;
    lastFusedLinks = collection.fusedLinks;
    lastFusedModels = collection.fusedModels;
    if (recorded == 1 || (recorded % 1800) == 0 || fusedChanged)
        Logging.Log("[self-recall] pose snapshot recorded: frames=%llu models=%u bones=%u materials=%u epoch=%llu singles=%u groups=%u static_models=%u parent_skipped=%u fused_links=%u fused_models=%u",
                    static_cast<unsigned long long>(recorded), collection.modelCount,
                    collection.boneCount, collection.materialCount,
                    static_cast<unsigned long long>(phase.epoch), phase.queuedSingles, phase.queuedGroups,
                    collection.staticModels, collection.parentSkipped, collection.fusedLinks, collection.fusedModels);
}

}  // namespace

bool presentationPose(void* actor, pure::Pose& out) {
    return appliedPlayerPose(actor, out);
}

void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    equipment::install(mainBase);
    PlayerMatrixHook::InstallAtOffset(kActorUpdateMatrix);
    ControllerMatrixHook::InstallAtOffset(kControllerMatrixAndVelocity);
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

void prepareScene(void* nativeScene, std::uint64_t epoch) {
    if (!pose_session::active()) return;
    Control control;
    if (!g_control.snapshot(control) || !control.worldGeneration) return;
    const auto scene = totk::engine::resolveScene(g_mainBase);
    if (!scene || scene.value.token.value != control.scene) return;
    using PlayerLink = const void* (*)(std::uintptr_t);
    const auto* link = reinterpret_cast<PlayerLink>(g_mainBase + kResidentPlayerLink)(
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
    if (!collection.appendModel(player, true) || !collection.appendOwnedModels(player)) {
        reject(collection.error, epoch);
        return;
    }
    auto historical = pose_render::currentFrame(
        reinterpret_cast<const void*>(collection.views[0].identity.unit), epoch);
    if (!historical) return;
    equipment_effects::publishLive({collection.actors.data() + 1, collection.actorCount - 1u});
    pose_render::suppressEquipment(epoch, {collection.views.data() + collection.bodyModels,
                               static_cast<std::size_t>(collection.modelCount - collection.bodyModels)});
    if (!collection.useHistoricalEquipment(player, nativeScene, historical.get()->animation)) {
        reject(collection.error, epoch);
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
    if (!rendering && !g_enabled.load(std::memory_order_acquire)) {
        skipped(Gate::Disabled, phase.epoch); return;
    }
    if (!rendering && g_suspended.load(std::memory_order_acquire)) {
        skipped(Gate::Suspended, phase.epoch); return;
    }
    auto* history = pose_storage::history();
    if (!history) { skipped(Gate::Storage, phase.epoch); return; }
    if (g_collecting.test_and_set(std::memory_order_acquire)) { skipped(Gate::Busy, phase.epoch); return; }
    struct Unlock { ~Unlock() { g_collecting.clear(std::memory_order_release); } } unlock;
    if (!rendering && g_suspended.load(std::memory_order_acquire)) {
        skipped(Gate::Suspended, phase.epoch); return;
    }
    ModelFrameTime frameTime{};
    if (!g_modelTime.snapshot(frameTime) || frameTime.epoch != phase.epoch) {
        skipped(Gate::FrameTime, phase.epoch, frameTime.epoch); return;
    }
    if (!rendering && (frameTime.time.status != pure::GameTimeStatus::Running ||
                       frameTime.time.serial == g_lastTimeSerial)) {
        skipped(Gate::Clock, phase.epoch, static_cast<unsigned>(frameTime.time.status) |
            ((frameTime.time.serial == g_lastTimeSerial ? 1ull : 0ull) << 8)); return;
    }
    Control control{};
    if (!g_control.snapshot(control) || (!rendering && !control.enabled) || !control.worldGeneration) {
        skipped(Gate::Control, phase.epoch, (std::uint64_t{control.worldGeneration} << 1) | control.enabled); return;
    }
    const auto scene = totk::engine::resolveScene(g_mainBase);
    if (!scene || scene.value.token.value != control.scene) {
        skipped(Gate::Scene, phase.epoch, scene ? scene.value.token.value : 0); return;
    }
    using PlayerLink = const void* (*)(std::uintptr_t);
    const auto* link = reinterpret_cast<PlayerLink>(g_mainBase + kResidentPlayerLink)(
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
    if (read<const void*>(bodyRoot, 0x60) != phase.scene) {
        skipped(Gate::OtherModelScene, phase.epoch, reinterpret_cast<std::uintptr_t>(phase.scene)); return;
    }
    collection.actors[0] = player;
    collection.actorCount = 1;
    if (!collection.appendModel(player, true)) { reject(collection.error, phase.epoch); return; }
    if (!rendering && phase.epoch == g_lastEpoch) { skipped(Gate::Duplicate, phase.epoch); return; }
    pure::ActorFrameTicket ticket{};
    if (!rendering && !pairCompletedActorPose(control, player, history, phase.epoch, ticket)) return;
    if (!collection.appendOwnedModels(player)) {
        reject(collection.error, phase.epoch); return;
    }
    auto historical = rendering ? pose_render::currentFrame(
        reinterpret_cast<const void*>(collection.views[0].identity.unit), phase.epoch)
        : pure::RenderFrameStore::Lease{};
    if (rendering) {
        if (!historical) return;
        if (!collection.useHistoricalEquipment(player, phase.scene, historical.get()->animation)) {
            reject(collection.error, phase.epoch); return;
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
        g_suspended.load(std::memory_order_acquire)) {
        skipped(Gate::FinalControl, phase.epoch); return;
    }
    recordOwnedFrame(phase, control, frameTime, player, collection, ticket, history);
}

void logDiagnostics() {
    const auto hooks = frame::diagnostics();
    static std::uint64_t lastReport = 0;
    if (lastReport && hooks.frames - lastReport < 60) return;
    lastReport = hooks.frames;
    pure::GameTimeSnapshot clock{};
    const bool haveClock = game_clock::snapshot(clock);
    Logging.Log("[self-recall] CAPTURE_STATE frames=%llu joined=%llu single=%llu multi=%llu owner_fail=%llu join_fail=%llu recorded=%llu rejected=%llu enabled=%u suspended=%u clear=%u clock=%u serial=%llu",
        static_cast<unsigned long long>(hooks.frames), static_cast<unsigned long long>(hooks.completedScenes),
        static_cast<unsigned long long>(hooks.singleCompletions), static_cast<unsigned long long>(hooks.multiCompletions),
        static_cast<unsigned long long>(hooks.rejectedOwners), static_cast<unsigned long long>(hooks.joinFailures),
        static_cast<unsigned long long>(g_recorded.load()), static_cast<unsigned long long>(g_rejections.load()),
        unsigned(g_enabled.load()), unsigned(g_suspended.load()), unsigned(g_clearRequested.load()),
        haveClock ? static_cast<unsigned>(clock.status) : 255u, static_cast<unsigned long long>(clock.serial));
    for (unsigned i = 0; i < g_gates.size(); ++i) {
        const auto count = g_gates[i].count.load(std::memory_order_relaxed);
        if (count) Logging.Log("[self-recall] CAPTURE_GATE name=%s total=%llu detail=%llu epoch=%llu", kGateNames[i],
            static_cast<unsigned long long>(count), static_cast<unsigned long long>(g_gates[i].detail.load()),
            static_cast<unsigned long long>(g_gates[i].epoch.load()));
    }
}

}  // namespace self_recall::pose_recorder
