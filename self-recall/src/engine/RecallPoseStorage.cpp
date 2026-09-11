#include "RecallPoseStorage.hpp"

#include <atomic>
#include <new>
#include <lib.hpp>

#include "RecallModelView.hpp"

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

}  // namespace

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

Diagnostics diagnostics() {
    const auto state = g_state.load(std::memory_order_acquire);
    return {state, kBytes};
}

}  // namespace self_recall::pose_storage
