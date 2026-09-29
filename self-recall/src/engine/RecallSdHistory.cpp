#include "RecallRuntimeEngine.hpp"
#include "RecallModelEngine.hpp"

#if SELF_RECALL_SD_HISTORY

#include <lib.hpp>
#include <nn/fs.h>
#include <nn/os.h>

#include <atomic>
#include <new>
#include <span>

namespace self_recall::sd_history {
namespace {

constexpr const char* kMount = "srsd";
constexpr const char* kDirectory = "srsd:/self-recall-alpha";
constexpr const char* kHistoryPath = "srsd:/self-recall-alpha/history.bin";
constexpr std::uint64_t kSecond = 1000000000ull;
constexpr std::size_t kStackBytes = 0x10000;

alignas(pure::PosePayloadBlock) std::byte g_cacheStorage[pure::kSpillCacheBytes];
alignas(pure::SpillGroup) std::byte g_groupStorage[sizeof(pure::SpillGroup) * pure::kHistoryCapacity];
std::byte g_ring[pure::kSpillWriteRingBytes];
std::byte g_io[pure::kSpillMaxGroupBytes];
alignas(pure::PoseSpill) std::byte g_spillObject[sizeof(pure::PoseSpill)];
std::atomic<pure::PoseSpill*> g_spill{nullptr};
std::atomic_flag g_constructing = ATOMIC_FLAG_INIT;

alignas(0x1000) std::byte g_stack[kStackBytes];
nn::os::ThreadType g_thread{};
std::atomic<bool> g_started{false};

struct LockGuard {
    std::atomic_flag& flag;
    explicit LockGuard(std::atomic_flag& f) : flag(f) { while (flag.test_and_set(std::memory_order_acquire)) {} }
    ~LockGuard() { flag.clear(std::memory_order_release); }
};

std::uint64_t ticksToNanoseconds(std::uint64_t ticks) {
    static const std::uint64_t frequency = nn::os::GetSystemTickFrequency();
    if (!frequency) return 0;
    return ticks / frequency * kSecond + ticks % frequency * kSecond / frequency;
}

class NnSpillFile final : public pure::SpillFile {
public:
    nn::fs::FileHandle handle{};
    bool open = false;
    unsigned lastResult = 0;

    bool write(std::uint64_t offset, std::span<const std::byte> bytes) override {
        const auto result = nn::fs::WriteFile(handle, static_cast<s64>(offset), bytes.data(), bytes.size(),
            nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
        if (result.IsFailure()) lastResult = result.GetInnerValueForDebug();
        return result.IsSuccess();
    }
    bool read(std::uint64_t offset, std::span<std::byte> bytes) override {
        const auto result = nn::fs::ReadFile(handle, static_cast<long>(offset), bytes.data(),
                                             static_cast<ulong>(bytes.size()));
        if (result.IsFailure()) lastResult = result.GetInnerValueForDebug();
        return result.IsSuccess();
    }
};

struct Worker {
    NnSpillFile history;
    bool mounted = false;
    unsigned consecutiveFailures = 0;
    std::uint64_t nextOpenAttempt = 0;
};
Worker g_worker;

bool openHistory() {
    auto& w = g_worker;
    auto result = nn::fs::OpenFile(&w.history.handle, kHistoryPath, nn::fs::OpenMode_ReadWrite);
    if (result.IsSuccess()) {
        long size = 0;
        if (nn::fs::GetFileSize(&size, w.history.handle).IsSuccess() &&
            static_cast<std::uint64_t>(size) >= pure::kSpillFileBytes) {
            w.history.open = true;
            return true;
        }
        nn::fs::CloseFile(w.history.handle);
        (void)nn::fs::DeleteFile(kHistoryPath);
    }
    result = nn::fs::CreateFile(kHistoryPath, static_cast<s64>(pure::kSpillFileBytes));
    if (result.IsSuccess()) result = nn::fs::OpenFile(&w.history.handle, kHistoryPath, nn::fs::OpenMode_ReadWrite);
    if (result.IsFailure()) {
        Logging.Log("[self-recall] SD_HISTORY_OPEN_FAILED result=%08x", result.GetInnerValueForDebug());
        return false;
    }
    w.history.open = true;
    return true;
}

void openFiles() {
    auto& w = g_worker;
    if (!w.mounted) {
        const auto result = nn::fs::MountSdCard(kMount);
        if (result.IsFailure()) {
            Logging.Log("[self-recall] SD_MOUNT_FAILED result=%08x", result.GetInnerValueForDebug());
            return;
        }
        w.mounted = true;
        (void)nn::fs::CreateDirectory(kDirectory);
    }
    if (!w.history.open && openHistory()) {
        w.consecutiveFailures = 0;
        g_spill.load(std::memory_order_acquire)->setEnabled(true);
    }
}

void run(void*) {
    auto* spill = g_spill.load(std::memory_order_acquire);
    auto& w = g_worker;
    for (;;) {
        const auto now = nowNanoseconds();
        if (!w.history.open && now >= w.nextOpenAttempt) {
            openFiles();
            w.nextOpenAttempt = now + 5 * kSecond;
        }
        bool worked = false;
        if (w.history.open) {
            for (unsigned i = 0; i < 8; ++i) {
                const auto report = spill->pump(w.history);
                if (!report) break;
                worked = true;
                const bool failed = report.kind == pure::SpillIoKind::WriteFailed ||
                                    report.kind == pure::SpillIoKind::ReadFailed;
                w.consecutiveFailures = failed ? w.consecutiveFailures + 1 : 0;
                if (w.consecutiveFailures >= 3) {
                    spill->setEnabled(false);
                    nn::fs::CloseFile(w.history.handle);
                    w.history.open = false;
                    Logging.Log("[self-recall] SD_HISTORY_DISABLED result=%08x", w.history.lastResult);
                    break;
                }
            }
        }
        if (!worked) svcSleepThread(2000000);
    }
}

}

std::uint64_t nowNanoseconds() { return ticksToNanoseconds(svcGetSystemTick()); }

pure::PoseSpill* spill() {
    if (auto* existing = g_spill.load(std::memory_order_acquire)) return existing;
    LockGuard lock(g_constructing);
    if (auto* existing = g_spill.load(std::memory_order_acquire)) return existing;
    auto* cache = reinterpret_cast<pure::PosePayloadBlock*>(g_cacheStorage);
    for (unsigned i = 0; i < pure::kSpillCacheBlockCount; ++i)
        ::new (static_cast<void*>(cache + i)) pure::PosePayloadBlock;
    auto* groups = reinterpret_cast<pure::SpillGroup*>(g_groupStorage);
    for (unsigned i = 0; i < pure::kHistoryCapacity; ++i) ::new (static_cast<void*>(groups + i)) pure::SpillGroup;
    auto* created = ::new (static_cast<void*>(g_spillObject)) pure::PoseSpill(
        {cache, pure::kSpillCacheBlockCount}, {groups, pure::kHistoryCapacity}, g_ring, g_io);
    g_spill.store(created, std::memory_order_release);
    return created;
}

void start() {
    if (g_started.exchange(true, std::memory_order_acq_rel)) return;
    (void)spill();
    const auto priority = nn::os::GetThreadPriority(nn::os::GetCurrentThread());
    const auto result = nn::os::CreateThread(&g_thread, &run, nullptr, g_stack, kStackBytes, priority);
    if (result.IsFailure()) {
        Logging.Log("[self-recall] SD_HISTORY_THREAD_FAILED result=%08x", result.GetInnerValueForDebug());
        return;
    }
    nn::os::SetThreadName(&g_thread, "self-recall-sd");
    nn::os::StartThread(&g_thread);
}

void playback(bool active, std::uint32_t generation, std::uint64_t serial) {
    if (auto* current = g_spill.load(std::memory_order_acquire)) current->setPlayback(active, generation, serial);
}

}

#endif
