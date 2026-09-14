#include "StartupTrace.hpp"

#if SELF_RECALL_ROMFS_DIAGNOSTIC

#include <lib.hpp>
#include <nn/fs.h>
#include <nn/util.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace self_recall::startup_trace {
namespace {

constexpr std::uint64_t kTraceCapacity = 64 * 1024;
nn::fs::FileHandle g_file{};
std::atomic<bool> g_ready{};
std::atomic_flag g_writer = ATOMIC_FLAG_INIT;
bool g_attempted{};
std::uint64_t g_offset{};
std::uint64_t g_sequence{};
char g_path[128]{};

void stop(const char* step, unsigned result) {
    if (g_ready.exchange(false, std::memory_order_acq_rel)) nn::fs::CloseFile(g_file);
    Logging.Log("[self-recall-diag] trace stopped step=%s result=%08x", step, result);
}

}

void mark(const char* stage, std::uint64_t value0, std::uint64_t value1) {
    if (!g_ready.load(std::memory_order_acquire) ||
        g_writer.test_and_set(std::memory_order_acquire)) return;

    char line[256]{};
    const int written = nn::util::SNPrintf(
        line, sizeof(line), "%04llu tick=%016llx %s value0=%016llx value1=%016llx\n",
        static_cast<unsigned long long>(g_sequence++),
        static_cast<unsigned long long>(svcGetSystemTick()), stage ? stage : "null-stage",
        static_cast<unsigned long long>(value0), static_cast<unsigned long long>(value1));
    const std::size_t bytes = written > 0
        ? std::min<std::size_t>(static_cast<std::size_t>(written), sizeof(line) - 1)
        : 0;
    if (!bytes || g_offset + bytes > kTraceCapacity) {
        stop("capacity", 0);
        g_writer.clear(std::memory_order_release);
        return;
    }

    auto result = nn::fs::WriteFile(g_file, static_cast<s64>(g_offset), line, bytes,
                                    nn::fs::WriteOption{0});
    if (result.IsFailure()) {
        stop("write", result.GetInnerValueForDebug());
        g_writer.clear(std::memory_order_release);
        return;
    }
    result = nn::fs::FlushFile(g_file);
    if (result.IsFailure()) {
        stop("flush", result.GetInnerValueForDebug());
        g_writer.clear(std::memory_order_release);
        return;
    }
    g_offset += bytes;
    g_writer.clear(std::memory_order_release);
}

void begin() {
    if (g_attempted) return;
    g_attempted = true;

    auto result = nn::fs::MountSdCard("selfrecalldiag");
    if (result.IsFailure()) {
        stop("mount", result.GetInnerValueForDebug());
        return;
    }

    const auto nonce = static_cast<unsigned long long>(svcGetSystemTick());
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        nn::util::SNPrintf(g_path, sizeof(g_path),
            "selfrecalldiag:/self-recall-romfs-diag-%016llx-%02u.log", nonce, attempt);
        result = nn::fs::CreateFile(g_path, static_cast<s64>(kTraceCapacity));
        if (result.IsSuccess()) break;
    }
    if (result.IsFailure()) {
        stop("create", result.GetInnerValueForDebug());
        return;
    }
    result = nn::fs::OpenFile(&g_file, g_path, nn::fs::OpenMode_Write);
    if (result.IsFailure()) {
        stop("open", result.GetInnerValueForDebug());
        return;
    }

    g_ready.store(true, std::memory_order_release);
    mark("00 trace-open", kTraceCapacity, 0);
    Logging.Log("[self-recall-diag] trace ready path=%s", g_path);
}

bool ready() { return g_ready.load(std::memory_order_acquire); }
const char* path() { return g_path; }

}

#endif
