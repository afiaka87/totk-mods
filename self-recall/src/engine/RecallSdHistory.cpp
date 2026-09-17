#include "RecallRuntimeEngine.hpp"
#include "RecallModelEngine.hpp"

#if SELF_RECALL_SD_HISTORY

#include <lib.hpp>
#include <nn/fs.h>
#include <nn/os.h>
#include <nn/util.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <new>
#include <span>

namespace self_recall::sd_history {
namespace {

constexpr const char* kMount = "srsd";
constexpr const char* kDirectory = "srsd:/self-recall-alpha";
constexpr const char* kHistoryPath = "srsd:/self-recall-alpha/history.bin";
constexpr std::uint64_t kLogCapacity = 8ull * 1024 * 1024;
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

struct Event {
    std::uint64_t nanoseconds = 0;
    const char* name = nullptr;
    std::uint64_t a = 0, b = 0;
};
std::array<Event, 128> g_events{};
std::uint32_t g_eventHead = 0, g_eventTail = 0;
std::atomic<std::uint64_t> g_eventsDropped{0};
std::atomic_flag g_eventLock = ATOMIC_FLAG_INIT;

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
    std::uint64_t nanoseconds() override { return nowNanoseconds(); }
};

// Worker-thread-only state.
struct Worker {
    NnSpillFile history;
    nn::fs::FileHandle log{};
    bool logOpen = false;
    bool mounted = false;
    std::uint64_t logOffset = 0;
    char buffer[16 * 1024]{};
    std::size_t buffered = 0;
    unsigned consecutiveFailures = 0;
    std::uint64_t nextOpenAttempt = 0;
    std::uint64_t lastSummary = 0;
    std::uint64_t lastWrites = 0, lastReads = 0;
};
Worker g_worker;

void flushLog() {
    auto& w = g_worker;
    if (!w.logOpen || !w.buffered) return;
    if (w.logOffset + w.buffered > kLogCapacity) {
        w.buffered = 0;
        return;
    }
    const auto result = nn::fs::WriteFile(w.log, static_cast<s64>(w.logOffset), w.buffer, w.buffered,
                                          nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
    if (result.IsFailure()) {
        Logging.Log("[self-recall] SD_LOG_WRITE_FAILED result=%08x", result.GetInnerValueForDebug());
        nn::fs::CloseFile(w.log);
        w.logOpen = false;
    } else {
        w.logOffset += w.buffered;
    }
    w.buffered = 0;
}

__attribute__((format(printf, 1, 2)))
void line(const char* format, ...) {
    auto& w = g_worker;
    if (!w.logOpen) return;
    if (sizeof(w.buffer) - w.buffered < 512) flushLog();
    std::va_list args;
    va_start(args, format);
    const int written = nn::util::VSNPrintf(w.buffer + w.buffered, sizeof(w.buffer) - w.buffered, format, args);
    va_end(args);
    if (written > 0) w.buffered += std::min<std::size_t>(static_cast<std::size_t>(written), sizeof(w.buffer) - w.buffered - 1);
}

std::uint64_t milliseconds(std::uint64_t nanoseconds) { return nanoseconds / 1000000ull; }

bool openLog() {
    auto& w = g_worker;
    char path[96];
    const auto nonce = static_cast<unsigned long long>(svcGetSystemTick());
    nn::util::SNPrintf(path, sizeof(path), "%s/log-%016llx.txt", kDirectory, nonce);
    auto result = nn::fs::CreateFile(path, 0);
    if (result.IsSuccess())
        result = nn::fs::OpenFile(&w.log, path, nn::fs::OpenMode_Write | nn::fs::OpenMode_Append);
    if (result.IsFailure()) {
        Logging.Log("[self-recall] SD_LOG_OPEN_FAILED result=%08x", result.GetInnerValueForDebug());
        return false;
    }
    w.logOpen = true;
    w.logOffset = 0;
    Logging.Log("[self-recall] SD_LOG path=%s", path);
    return true;
}

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
    const auto start = nowNanoseconds();
    result = nn::fs::CreateFile(kHistoryPath, static_cast<s64>(pure::kSpillFileBytes));
    if (result.IsSuccess()) result = nn::fs::OpenFile(&w.history.handle, kHistoryPath, nn::fs::OpenMode_ReadWrite);
    line("history_create result=%08x ms=%llu\n", result.GetInnerValueForDebug(),
         static_cast<unsigned long long>(milliseconds(nowNanoseconds() - start)));
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
    if (!w.logOpen && openLog()) {
        line("self-recall sd-history alpha ram_seconds=%u pool_bytes=%u cache_bytes=%u file_bytes=%llu slots=%u\n",
             pure::kSdHistoryRamSeconds, pure::kPosePayloadArenaBytes, pure::kSpillCacheBytes,
             static_cast<unsigned long long>(pure::kSpillFileBytes), pure::kHistoryCapacity);
    }
    if (!w.history.open && openHistory()) {
        w.consecutiveFailures = 0;
        g_spill.load(std::memory_order_acquire)->setEnabled(true);
        line("sd_enabled t_ms=%llu\n", static_cast<unsigned long long>(milliseconds(nowNanoseconds())));
    }
    flushLog();
}

void drainEvents() {
    for (;;) {
        Event event;
        {
            LockGuard lock(g_eventLock);
            if (g_eventTail == g_eventHead) break;
            event = g_events[g_eventTail];
            g_eventTail = (g_eventTail + 1) % g_events.size();
        }
        line("E t_ms=%llu %s a=%llu b=%llu\n", static_cast<unsigned long long>(milliseconds(event.nanoseconds)),
             event.name ? event.name : "?", static_cast<unsigned long long>(event.a),
             static_cast<unsigned long long>(event.b));
    }
}

void logIo(const pure::SpillIoReport& report) {
    constexpr const char* kinds[]{"none", "W", "R", "X", "W_FAIL", "R_FAIL", "INVALID"};
    line("%s t_ms=%llu grp=%u serial=%llu bytes=%u off=%llu us=%llu\n", kinds[unsigned(report.kind)],
         static_cast<unsigned long long>(milliseconds(nowNanoseconds())), report.group,
         static_cast<unsigned long long>(report.serial), report.bytes,
         static_cast<unsigned long long>(report.offset),
         static_cast<unsigned long long>(report.nanoseconds / 1000ull));
}

void summary(pure::PoseSpill& spill) {
    auto& stats = spill.stats();
    auto& w = g_worker;
    const auto writes = stats.writes.load(std::memory_order_relaxed);
    const auto reads = stats.reads.load(std::memory_order_relaxed);
    if (writes == w.lastWrites && reads == w.lastReads && !spill.pendingBytes() && !spill.playbackActive()) return;
    w.lastWrites = writes;
    w.lastReads = reads;
    line("S t_ms=%llu pool_free_kib=%llu pending_kib=%llu cache_kib=%llu writes=%llu reads=%llu "
         "max_write_us=%llu max_read_us=%llu released=%llu ram_only=%llu lost=%llu overwritten=%llu trimmed=%llu rejected=%llu "
         "failures=%llu playback=%u dropped_events=%llu\n",
         static_cast<unsigned long long>(milliseconds(nowNanoseconds())),
         static_cast<unsigned long long>(stats.poolBlocksAvailable.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(spill.pendingBytes() / 1024),
         static_cast<unsigned long long>(stats.cacheBlocksUsed.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(writes), static_cast<unsigned long long>(reads),
         static_cast<unsigned long long>(stats.maxWriteNanoseconds.exchange(0, std::memory_order_relaxed) / 1000ull),
         static_cast<unsigned long long>(stats.maxReadNanoseconds.exchange(0, std::memory_order_relaxed) / 1000ull),
         static_cast<unsigned long long>(stats.released.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(stats.ramOnly.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(stats.lost.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(stats.overwritten.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(stats.trimmedForSpace.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(stats.rejectedWhileWriting.load(std::memory_order_relaxed)),
         static_cast<unsigned long long>(stats.failures.load(std::memory_order_relaxed)),
         unsigned(spill.playbackActive()),
         static_cast<unsigned long long>(g_eventsDropped.load(std::memory_order_relaxed)));
}

void run(void*) {
    auto* spill = g_spill.load(std::memory_order_acquire);
    auto& w = g_worker;
    for (;;) {
        const auto now = nowNanoseconds();
        if ((!w.history.open || !w.logOpen) && now >= w.nextOpenAttempt) {
            openFiles();
            w.nextOpenAttempt = now + 5 * kSecond;
        }
        drainEvents();
        bool worked = false;
        if (w.history.open) {
            for (unsigned i = 0; i < 8; ++i) {
                const auto report = spill->pump(w.history);
                if (!report) break;
                worked = true;
                logIo(report);
                const bool failed = report.kind == pure::SpillIoKind::WriteFailed ||
                                    report.kind == pure::SpillIoKind::ReadFailed;
                w.consecutiveFailures = failed ? w.consecutiveFailures + 1 : 0;
                if (failed) line("io_failure result=%08x consecutive=%u\n", w.history.lastResult, w.consecutiveFailures);
                if (w.consecutiveFailures >= 3) {
                    spill->setEnabled(false);
                    nn::fs::CloseFile(w.history.handle);
                    w.history.open = false;
                    line("sd_disabled after repeated failures\n");
                    Logging.Log("[self-recall] SD_HISTORY_DISABLED result=%08x", w.history.lastResult);
                    break;
                }
            }
        }
        if (now - w.lastSummary >= kSecond) {
            summary(*spill);
            flushLog();
            w.lastSummary = now;
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
    Logging.Log("[self-recall] SD_HISTORY_THREAD result=%08x priority=%d ram_seconds=%u",
                result.GetInnerValueForDebug(), priority, pure::kSdHistoryRamSeconds);
    if (result.IsFailure()) return;
    nn::os::SetThreadName(&g_thread, "self-recall-sd");
    nn::os::StartThread(&g_thread);
}

void event(const char* name, std::uint64_t a, std::uint64_t b) {
    LockGuard lock(g_eventLock);
    const auto next = (g_eventHead + 1) % g_events.size();
    if (next == g_eventTail) {
        g_eventsDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_events[g_eventHead] = {nowNanoseconds(), name, a, b};
    g_eventHead = next;
}

void playback(bool active, std::uint32_t generation, std::uint64_t serial) {
    if (auto* current = g_spill.load(std::memory_order_acquire)) current->setPlayback(active, generation, serial);
}

}

#endif
