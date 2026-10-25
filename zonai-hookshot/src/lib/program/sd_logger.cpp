// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "sd_logger.hpp"

#include <lib.hpp>
#include <nn/fs.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace zonai_hookshot::log {
namespace {

// A private mount name; "sdcard" belongs to other mods sharing the process.
constexpr const char* kMount = "zhlog";
constexpr const char* kDirectory = "zhlog:/zonai-hookshot";
constexpr const char* kPath = "zhlog:/zonai-hookshot/hookshot.log";
constexpr std::size_t kCapacity = 0x10000;
// Earlier sessions stay in the file, so a failed boot survives one relaunch.
constexpr s64 kKeepBytes = 4 << 20;
constexpr unsigned kSettleTicks = 180, kRetryTicks = 120, kMaxTries = 10, kFlushTicks = 30;

enum State : int { Waiting, Open, Failed };
std::atomic<int> g_state{Waiting};
// Writers retry briefly, then drop the line rather than spin behind a preempted holder.
std::atomic<bool> g_lock{};
bool tryLock() {
    for (unsigned i = 0; i < 4096; ++i)
        if (!g_lock.exchange(true, std::memory_order_acquire)) return true;
    return false;
}
void unlock() { g_lock.store(false, std::memory_order_release); }
char g_buffer[kCapacity];
std::size_t g_used{};
std::atomic<unsigned> g_dropped{};
char g_flush[kCapacity];
std::atomic<bool> g_servicing{};
nn::fs::FileHandle g_file{};
s64 g_offset{};
bool g_mounted{};
unsigned g_ticks{}, g_tries{}, g_nextTry{kSettleTicks}, g_lastFlush{};

bool write(const char* data, std::size_t size) {
    const auto result = nn::fs::WriteFile(g_file, g_offset, data, size,
        nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
    if (result.IsFailure()) return false;
    g_offset += static_cast<s64>(size);
    return true;
}

nn::Result open() {
    if (!g_mounted) {
        const auto result = nn::fs::MountSdCard(kMount);
        if (result.IsFailure()) return result;
        g_mounted = true;
    }
    (void)nn::fs::CreateDirectory(kDirectory);
    // Append mode: without it the SDK refuses writes past the end of the file.
    constexpr int kMode = nn::fs::OpenMode_Write | nn::fs::OpenMode_Append;
    auto result = nn::fs::OpenFile(&g_file, kPath, kMode);
    if (result.IsFailure()) {
        result = nn::fs::CreateFile(kPath, 0);
        if (result.IsSuccess()) result = nn::fs::OpenFile(&g_file, kPath, kMode);
        if (result.IsFailure()) return result;
    }
    s64 size{};
    if (nn::fs::GetFileSize(&size, g_file).IsFailure() || size > kKeepBytes) {
        result = nn::fs::SetFileSize(g_file, 0);
        if (result.IsFailure()) {
            nn::fs::CloseFile(g_file);
            return result;
        }
        size = 0;
    }
    g_offset = size;
    return result;
}

void flush() {
    if (!tryLock()) return;
    const auto size = g_used;
    if (!size || (g_ticks - g_lastFlush < kFlushTicks && size < kCapacity / 2)) {
        unlock();
        return;
    }
    std::memcpy(g_flush, g_buffer, size);
    g_used = 0;
    unlock();
    g_lastFlush = g_ticks;
    bool ok = write(g_flush, size);
    if (const auto dropped = g_dropped.exchange(0, std::memory_order_relaxed)) {
        char line[96];
        const int length = std::snprintf(line, sizeof(line),
            "[zonai-hookshot] SD_LOG dropped=%u lines (buffer full or busy)\n", dropped);
        ok = ok && (length <= 0 || write(line, static_cast<std::size_t>(length)));
    }
    if (!ok) {
        g_state.store(Failed, std::memory_order_release);
        nn::fs::CloseFile(g_file);
        Logging.Log("[zonai-hookshot] SD_LOG write failed; debug output only");
    }
}

}  // namespace

void SdFileLogger::LogRaw(std::string_view string) {
    if (g_state.load(std::memory_order_acquire) == Failed) return;
    const bool newline = string.empty() || string.back() != '\n';
    if (!tryLock()) {
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (string.size() + newline > kCapacity - g_used) {
        unlock();
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::memcpy(g_buffer + g_used, string.data(), string.size());
    g_used += string.size();
    if (newline) g_buffer[g_used++] = '\n';
    unlock();
}

void sdLogService() {
    if (g_servicing.exchange(true, std::memory_order_acquire)) return;
    ++g_ticks;
    if (g_state.load(std::memory_order_relaxed) == Waiting && g_ticks >= g_nextTry) {
        const auto result = open();
        if (result.IsSuccess()) {
            char header[128];
            const int length = std::snprintf(header, sizeof(header),
                "\n[zonai-hookshot] SD_LOG session start offset=%lld tick=%llu\n",
                static_cast<long long>(g_offset),
                static_cast<unsigned long long>(svcGetSystemTick()));
            g_state.store(Open, std::memory_order_release);
            if (length > 0) write(header, static_cast<std::size_t>(length));
            g_lastFlush = g_ticks - kFlushTicks;
        } else if (++g_tries >= kMaxTries) {
            g_state.store(Failed, std::memory_order_release);
            Logging.Log("[zonai-hookshot] SD_LOG unavailable result=%08x tries=%u; debug output only",
                        result.GetInnerValueForDebug(), g_tries);
        } else {
            g_nextTry = g_ticks + kRetryTicks;
            Logging.Log("[zonai-hookshot] SD_LOG retry result=%08x try=%u",
                        result.GetInnerValueForDebug(), g_tries);
        }
    }
    if (g_state.load(std::memory_order_relaxed) == Open) flush();
    g_servicing.store(false, std::memory_order_release);
}

}  // namespace zonai_hookshot::log
