#include "RecallModelEngine.hpp"

#include <atomic>
#include <new>
#include <lib.hpp>

#include "RecallBase.hpp"
#include "RecallVisual.hpp"

namespace self_recall::pose_storage {
namespace {

constexpr auto kSlotBytes = sizeof(pure::PoseHistorySlot) * pure::kHistoryCapacity;
constexpr auto kBytes = kSlotBytes + pure::kPosePayloadArenaBytes;
static_assert(kBytes + sizeof(pure::PoseHistory) + sizeof(model::CaptureWorkspace) <=
              pure::kPoseHistoryByteLimit);

std::atomic<State> g_state{State::WaitingForGameplay};
std::atomic<bool> g_preparationAllowed{false};
alignas(pure::PoseHistorySlot) std::byte g_slotStorage[kSlotBytes]{};
alignas(pure::PosePayloadBlock) std::byte g_payloadStorage[pure::kPosePayloadArenaBytes];
alignas(pure::PoseHistory) std::byte g_historyObject[sizeof(pure::PoseHistory)]{};
pure::PoseHistory* g_history = nullptr;

}

void setPreparationAllowed(bool ready) {
    g_preparationAllowed.store(ready, std::memory_order_release);
}

State prepare() {
    if (!g_preparationAllowed.load(std::memory_order_acquire))
        return g_state.load(std::memory_order_acquire);
    auto expected = State::WaitingForGameplay;
    if (!g_state.compare_exchange_strong(expected, State::Constructing,
                                         std::memory_order_acq_rel)) return expected;
    auto* slots = reinterpret_cast<pure::PoseHistorySlot*>(g_slotStorage);
    for (std::uint32_t i = 0; i < pure::kHistoryCapacity; ++i)
        ::new (static_cast<void*>(slots + i)) pure::PoseHistorySlot;
    auto* blocks = reinterpret_cast<pure::PosePayloadBlock*>(g_payloadStorage);
    for (unsigned i = 0; i < pure::kPosePayloadBlockCount; ++i)
        ::new (static_cast<void*>(blocks + i)) pure::PosePayloadBlock;
    g_history = ::new (static_cast<void*>(g_historyObject))
        pure::PoseHistory(slots, pure::kHistoryCapacity, {blocks, pure::kPosePayloadBlockCount});
    g_state.store(State::Ready, std::memory_order_release);
    Logging.Log("[self-recall] pose storage ready: owner=module_bss bytes=%llu frames=%u bone_limit=%u",
                static_cast<unsigned long long>(kBytes),
                static_cast<unsigned>(pure::kHistoryCapacity),
                static_cast<unsigned>(pure::kPoseBoneLimit));
    return State::Ready;
}

pure::PoseHistory* history() {
    return g_state.load(std::memory_order_acquire) == State::Ready ? g_history : nullptr;
}

}

namespace self_recall::pose_session {
namespace {
alignas(pure::PosePlayback) std::byte g_storage[sizeof(pure::PosePlayback)]{};
pure::PosePlayback* g_playback = nullptr;
pure::FrameMailbox<pure::PosePresentation> g_selected;
pure::PresentationFrameLatch<pure::PosePresentation> g_presentation;
std::atomic<std::uint64_t> g_sessionSerial{0};
std::atomic<bool> g_active{false};
pure::AppliedClimbMailbox g_applied;
std::atomic<bool> g_haveApplied{false};
std::atomic<std::uint64_t> g_publishMisses{0};
}

void initialize() {
    if (!g_playback) g_playback = ::new (static_cast<void*>(g_storage)) pure::PosePlayback;
}

BeginResult begin(std::uint32_t world, const pure::GameTimeSnapshot& clock) {
    if (!g_playback) return {};
    if (pose_recorder::clearPending()) {
        reset(false);
        return {false, pure::PosePlaybackStatus::NoHistory};
    }
    if (!pose_recorder::trySuspend()) return {true, pure::PosePlaybackStatus::NoHistory};
    const auto* history = pose_storage::history();
    const auto result = history ? g_playback->begin(*history, world, clock)
                                : pure::PosePlaybackStatus::NoHistory;
    if (result != pure::PosePlaybackStatus::Ready) {
        reset(false);
        return {false, result};
    }
    pose_render::begin();
    const auto session = g_sessionSerial.fetch_add(1, std::memory_order_acq_rel) + 1;
    g_haveApplied.store(false, std::memory_order_release);
    const auto key = g_playback->presentation();
    if (!key || !g_selected.publish(key) || !g_presentation.publish(session, clock.serial, key))
        return {true, pure::PosePlaybackStatus::Ready};
    g_active.store(true, std::memory_order_release);
    return {false, pure::PosePlaybackStatus::Ready};
}

pure::PosePlayback* playback() { return g_playback; }

bool publishSelected() {
    if (!g_playback || !active()) return false;
    const auto key = g_playback->presentation();
    return key && g_selected.publish(key);
}

void reset(bool clearHistory) {
    g_active.store(false, std::memory_order_release);
    g_haveApplied.store(false, std::memory_order_release);
    g_publishMisses.store(0, std::memory_order_relaxed);
    pose_render::reset();
    if (g_playback) g_playback->reset();
    pose_recorder::resume(clearHistory);
}

pure::PoseReadLease acquireSelected(pure::PosePresentation* presentation) {
    if (!g_active.load(std::memory_order_acquire)) return {};
    pure::PosePresentation key;
    if (!g_selected.snapshot(key)) return {};
    const auto* history = pose_storage::history();
    if (!history) return {};
    auto lease = history->acquire(key.key);
    if (!g_active.load(std::memory_order_acquire)) return {};
    if (lease && presentation) *presentation = key;
    return lease;
}

bool active() { return g_active.load(std::memory_order_acquire); }

pure::PoseReadLease acquirePresentation(pure::PosePresentation* presentation) {
    if (!active()) return {};
    const auto session = g_sessionSerial.load(std::memory_order_acquire);
    const auto key = g_presentation.snapshot(session);
    const auto* history = pose_storage::history();
    if (!history || !key) return {};
    auto lease = history->acquire(key.key);
    if (!active() || g_sessionSerial.load(std::memory_order_acquire) != session) return {};
    if (lease && presentation) *presentation = key;
    return lease;
}

void latchPresentation(const pure::GameTimeSnapshot& clock) {
    if (!active() || !clock.serial) return;
    const auto session = g_sessionSerial.load(std::memory_order_acquire);
    pure::PosePresentation sample;
    auto selected = acquireSelected(&sample);
    if (!active() || g_sessionSerial.load(std::memory_order_acquire) != session) return;
    const auto key = selected ? g_presentation.publish(session, clock.serial, sample)
                              : pure::PosePresentation{};
    if (!key) {
        const auto count = g_publishMisses.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 4 || count % 300 == 0)
            Logging.Log("[self-recall] PRESENTATION_HELD count=%llu clock=%llu selected=%u",
                static_cast<unsigned long long>(count), static_cast<unsigned long long>(clock.serial),
                unsigned(bool(selected)));
        return;
    }
}

bool publishApplied(std::uintptr_t player, std::uint32_t actorId, std::uint32_t world,
                    const pure::HistorySample& sample) {
    if (!active()) return false;
    g_applied.publish({player, actorId, world, sample});
    g_haveApplied.store(true, std::memory_order_release);
    return true;
}

bool appliedPose(std::uintptr_t player, std::uint32_t actorId, std::uint32_t world, pure::Pose& out) {
    if (!active() || !g_haveApplied.load(std::memory_order_acquire)) return false;
    pure::AppliedClimb applied;
    if (!g_applied.snapshot(applied) || !pure::matchesAppliedPose(applied, player, actorId, world)) return false;
    pure::PosePresentation presentation;
    const auto shown = acquirePresentation(&presentation);
    if (!shown || shown.get()->header.worldGeneration != world) return false;
    out = shown.get()->header.route.pose;
    pure::shiftPosition(out.position, presentation.offset);
    return active() && g_haveApplied.load(std::memory_order_acquire);
}

}
