#include "RecallFrameHooks.hpp"

#include <atomic>
#include <cstring>
#include <lib.hpp>
#include "RecallModelCompletion.hpp"

namespace self_recall::frame {
namespace {

constexpr std::uintptr_t kModelFrameStart = 0x00973550;
constexpr std::uintptr_t kInvokeModelCalcQueue = 0x00981248;
constexpr std::uintptr_t kInvokeSingleModelQueue = 0x00970820;
constexpr std::uintptr_t kSceneCalcFrame = 0x00974D9C;
constexpr std::size_t kQueueOwner = 0x40;
constexpr std::size_t kQueueGroupCount = 0x58;
constexpr std::uintptr_t kSceneModelQueue = 0x42A0;
constexpr std::size_t kContextCompleted = 8;

Observers g_observers{};
std::atomic<std::uint64_t> g_epoch{0};
std::atomic<std::uint64_t> g_completed{0};
std::atomic<std::uint64_t> g_rejectedOwners{0};
std::atomic<std::uint64_t> g_singleCompleted{0}, g_multiCompleted{0}, g_joinFailures{0};
pure::ModelCompletionJoin<> g_join;
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

template <class T>
T read(const void* base, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

HOOK_DEFINE_TRAMPOLINE(ModelFrameStartHook) {
    static void Callback(void* manager) {
        Orig(manager);
        const auto epoch = g_epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
        g_join.beginFrame(epoch);
        if (g_observers.beginFrame) g_observers.beginFrame(epoch);
    }
};

void queueComplete(void* queue, void* context, pure::ModelQueueLane lane) {
    if (!read<std::uint8_t>(context, kContextCompleted)) return;

    std::atomic_thread_fence(std::memory_order_acquire);
    auto* scene = read<void*>(queue, kQueueOwner);
    if (!scene || reinterpret_cast<std::uintptr_t>(scene) + kSceneModelQueue !=
                      reinterpret_cast<std::uintptr_t>(queue)) {
        const auto count = g_rejectedOwners.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 4 || count % 1800 == 0)
            Logging.Log("[self-recall] model queue owner rejected: queue=%p scene=%p total=%llu",
                queue, scene, static_cast<unsigned long long>(count));
        return;
    }
    const auto epoch = g_epoch.load(std::memory_order_acquire);
    if (!epoch) return;
    (lane == pure::ModelQueueLane::Single ? g_singleCompleted : g_multiCompleted)
        .fetch_add(1, std::memory_order_relaxed);
    const auto joined = g_join.complete(reinterpret_cast<std::uintptr_t>(queue), epoch, lane);
    if (joined != pure::ModelJoinStatus::Complete) {
        if (joined != pure::ModelJoinStatus::Waiting) {
            const auto count = g_joinFailures.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count <= 4 || count % 1800 == 0)
                Logging.Log("[self-recall] model join rejected: status=%u queue=%p lane=%u epoch=%llu total=%llu",
                    static_cast<unsigned>(joined), queue, static_cast<unsigned>(lane),
                    static_cast<unsigned long long>(epoch), static_cast<unsigned long long>(count));
        }
        return;
    }
    g_completed.fetch_add(1, std::memory_order_relaxed);
    if (g_observers.modelsComplete) {
        const CompletedModelPhase phase{scene, queue, epoch,
                                         read<std::uint32_t>(queue, kQueueGroupCount),
                                         read<std::uint32_t>(queue, 0x20)};
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

HOOK_DEFINE_TRAMPOLINE(SceneCalcFrameHook) {
    static std::uintptr_t Callback(void* scene) {
        const auto epoch = g_epoch.load(std::memory_order_acquire);
        if (epoch && !g_join.registerScene(reinterpret_cast<std::uintptr_t>(scene) + kSceneModelQueue, epoch)) {
            const auto count = g_joinFailures.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count <= 4 || count % 1800 == 0)
                Logging.Log("[self-recall] model join scene limit: scene=%p epoch=%llu total=%llu", scene,
                    static_cast<unsigned long long>(epoch), static_cast<unsigned long long>(count));
        }
        if (epoch && g_observers.prepareScene) g_observers.prepareScene(scene, epoch);
        return Orig(scene);
    }
};

}  // namespace

void install(Observers observers) {
    g_observers = observers;
    ModelFrameStartHook::InstallAtOffset(kModelFrameStart);
    ModelCalcQueueHook::InstallAtOffset(kInvokeModelCalcQueue);
    ModelSingleQueueHook::InstallAtOffset(kInvokeSingleModelQueue);
    SceneCalcFrameHook::InstallAtOffset(kSceneCalcFrame);
}

Diagnostics diagnostics() {
    return {g_epoch.load(std::memory_order_acquire),
            g_completed.load(std::memory_order_relaxed),
            g_rejectedOwners.load(std::memory_order_relaxed),
            g_singleCompleted.load(std::memory_order_relaxed),
            g_multiCompleted.load(std::memory_order_relaxed),
            g_joinFailures.load(std::memory_order_relaxed)};
}

}  // namespace self_recall::frame
