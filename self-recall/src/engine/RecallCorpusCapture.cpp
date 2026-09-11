#include "RecallCorpusCapture.hpp"
#if SELF_RECALL_CORPUS_CAPTURE
#include "RecallCorpusQueue.hpp"
#include <lib.hpp>
#include <nn/fs.h>
#include <nn/os.h>
#include <cstdio>

namespace self_recall::corpus {
namespace {
static_assert(sizeof(pure::PoseFrameKey) == 16);
static_assert(offsetof(pure::PoseFrameHeader, key) == 0);
static_assert(offsetof(pure::PoseFrameHeader, elapsedNanoseconds) == 24);
pure::CorpusQueue<32, 68 * 1024> g_queue;
nn::os::ThreadType g_thread{};
alignas(4096) std::byte g_stack[64 * 1024];
std::atomic<bool> g_started{false}, g_accepting{false};
std::atomic<std::uint64_t> g_time{0};
constexpr std::uint64_t kFileLimit = 512ull * 1024 * 1024;
constexpr int kWriterSdkPriority = 20; // The SDK adds 28 to obtain kernel priority 48.

template<class T> auto bytesOf(const T& value) {
    return std::as_bytes(std::span{&value, 1});
}
void writer(void*) {
    auto result = nn::fs::MountSdCard("recalltrace");
    if (result.IsFailure()) {
        g_accepting.store(false);
        Logging.Log("[self-recall] CORPUS_FAILED phase=mount rc=%x", result.GetInnerValueForDebug());
        return;
    }
    char path[128];
    std::snprintf(path, sizeof(path), "recalltrace:/self-recall-%016llx.bin",
                  static_cast<unsigned long long>(svcGetSystemTick()));
    result = nn::fs::CreateFile(path, kFileLimit);
    nn::fs::FileHandle file{};
    if (result.IsSuccess()) result = nn::fs::OpenFile(&file, path, nn::fs::OpenMode_Write);
    if (result.IsFailure()) {
        g_accepting.store(false);
        Logging.Log("[self-recall] CORPUS_FAILED phase=open rc=%x", result.GetInnerValueForDebug());
        return;
    }
    Logging.Log("[self-recall] CORPUS_READY path=%s version=1 limit=%llu queue_bytes=%llu",
        path, static_cast<unsigned long long>(kFileLimit), static_cast<unsigned long long>(sizeof(g_queue)));
    std::uint64_t offset = 0, flushed = 0, records = 0, reportedDrops = 0;
    while (g_accepting.load(std::memory_order_acquire)) {
        const auto* record = g_queue.front();
        if (!record) { nn::os::SleepThread(nn::TimeSpan::FromNanoSeconds(10'000'000)); continue; }
        const auto size = sizeof(record->header) + record->header.bytes;
        if (size > kFileLimit - offset) break;
        result = nn::fs::WriteFile(file, offset, record, size, nn::fs::WriteOption{0});
        if (result.IsFailure()) break;
        offset += size; ++records;
        g_queue.pop();
        if (offset - flushed >= 1024 * 1024) {
            result = nn::fs::FlushFile(file);
            if (result.IsFailure()) break;
            flushed = offset;
        }
        const auto drops = g_queue.dropped();
        if (records % 1800 == 0 || drops != reportedDrops) {
            Logging.Log("[self-recall] CORPUS_PROGRESS bytes=%llu records=%llu dropped=%llu",
                static_cast<unsigned long long>(offset), static_cast<unsigned long long>(records),
                static_cast<unsigned long long>(drops));
            reportedDrops = drops;
        }
    }
    g_accepting.store(false, std::memory_order_release);
    const auto flush = nn::fs::FlushFile(file);
    const auto truncate = nn::fs::SetFileSize(file, offset);
    nn::fs::CloseFile(file);
    Logging.Log("[self-recall] CORPUS_CLOSED bytes=%llu records=%llu dropped=%llu rc=%x flush_rc=%x resize_rc=%x",
        static_cast<unsigned long long>(offset), static_cast<unsigned long long>(records),
        static_cast<unsigned long long>(g_queue.dropped()), result.GetInnerValueForDebug(),
        flush.GetInnerValueForDebug(), truncate.GetInnerValueForDebug());
}
template<std::size_t N> void submit(pure::CorpusKind kind, const std::array<std::span<const std::byte>, N>& parts) {
    if (g_accepting.load(std::memory_order_acquire)) g_queue.push(kind, g_time.load(), parts);
}
}

void start() {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) return;
    Logging.Log("[self-recall] CORPUS_THREAD sdk_priority=%d stack_bytes=%llu",
        kWriterSdkPriority, static_cast<unsigned long long>(sizeof(g_stack)));
    const auto result = nn::os::CreateThread(&g_thread, writer, nullptr, g_stack, sizeof(g_stack), kWriterSdkPriority, -2);
    if (result.IsFailure()) {
        Logging.Log("[self-recall] CORPUS_FAILED phase=thread rc=%x", result.GetInnerValueForDebug()); return;
    }
    g_accepting.store(true, std::memory_order_release);
    nn::os::SetThreadNamePointer(&g_thread, "SelfRecallCorpus");
    nn::os::StartThread(&g_thread);
}
void pose(const pure::RecordedPoseFrame& frame) {
    const auto& h = frame.header;
    g_time.store(h.elapsedNanoseconds);
    const std::array<std::uint32_t, 4> layout{sizeof(h), h.modelCount, h.boneCount, sizeof(frame.visible)};
    submit(pure::CorpusKind::Pose, std::array{bytesOf(layout), bytesOf(h),
        std::as_bytes(std::span{frame.models, h.modelCount}),
        std::as_bytes(std::span{frame.bones, h.boneCount}), bytesOf(frame.visible)});
}
void appearance(unsigned token, std::span<const std::byte> bytes) {
    submit(pure::CorpusKind::Appearance, std::array{bytesOf(token), bytes});
}
void binding(pure::PoseFrameKey key, std::span<const unsigned> tokens) {
    submit(pure::CorpusKind::Binding, std::array{bytesOf(key), std::as_bytes(tokens)});
}
void schema(unsigned asset, unsigned properties, unsigned enums, unsigned names,
            std::span<const std::byte> bytes) {
    const std::array<unsigned, 4> counts{asset, properties, enums, names};
    submit(pure::CorpusKind::Schema, std::array{bytesOf(counts), bytes});
}
} // namespace self_recall::corpus
#endif
