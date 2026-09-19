// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "sd_logger.hpp"

#include <lib.hpp>
#include <nn/fs.hpp>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace nn::fs {
Result SetFileSize(FileHandle handle, s64 size);
} // namespace nn::fs

namespace bivouac::log {
namespace {

constexpr std::size_t kCapacity = 16384;

// A spinlock, not an SDK mutex: appends take microseconds and this runs before the SDK is warm.
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
void lock() { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
void unlock() { g_lock.clear(std::memory_order_release); }
char g_buffer[kCapacity];
std::size_t g_used = 0;
unsigned g_dropped = 0;
char g_flushBuffer[kCapacity];
nn::fs::FileHandle g_file{};
bool g_open = false;
s64 g_offset = 0;

bool writeChunk(const char* data, std::size_t size) {
    const nn::fs::WriteOption flush{nn::fs::WriteOptionFlag_Flush};
    const Result result = nn::fs::WriteFile(
        g_file, g_offset, data, static_cast<u64>(size), flush);
    if (R_FAILED(result)) {
        return false;
    }
    g_offset += static_cast<s64>(size);
    return true;
}

} // namespace

void SdFileLogger::LogRaw(std::string_view string) {
    const std::size_t size = string.size();
    lock();
    if (size + 1 > kCapacity - g_used) {
        g_dropped++;
        unlock();
        return;
    }
    std::memcpy(g_buffer + g_used, string.data(), size);
    g_used += size;
    if (size == 0 || string[size - 1] != '\n') {
        g_buffer[g_used++] = '\n';
    }
    unlock();
}

bool sdLogOpen(const char* directory, const char* path) {
    if (g_open) {
        return true;
    }
    nn::fs::CreateDirectory(directory); // Already-existing is the normal case.
    nn::fs::FileHandle file;
    // Append mode: without it the SDK refuses writes past the end of the file.
    constexpr int kMode = nn::fs::OpenMode_Write | nn::fs::OpenMode_Append;
    Result result = nn::fs::OpenFile(&file, path, kMode);
    if (R_FAILED(result)) {
        result = nn::fs::CreateFile(path, 0);
        if (R_FAILED(result)) {
            return false;
        }
        result = nn::fs::OpenFile(&file, path, kMode);
        if (R_FAILED(result)) {
            return false;
        }
    }
    result = nn::fs::SetFileSize(file, 0);
    if (R_FAILED(result)) {
        nn::fs::CloseFile(file);
        return false;
    }
    g_file = file;
    g_offset = 0;
    g_open = true;
    return true;
}

void sdLogFlush() {
    if (!g_open) {
        return;
    }
    lock();
    const std::size_t size = g_used;
    const unsigned dropped = g_dropped;
    std::memcpy(g_flushBuffer, g_buffer, size);
    g_used = 0;
    g_dropped = 0;
    unlock();
    if (size > 0) {
        writeChunk(g_flushBuffer, size);
    }
    if (dropped > 0) {
        char line[64];
        const int length = std::snprintf(
            line, sizeof(line), "[bv] log: %u line(s) dropped (buffer full)\n", dropped);
        if (length > 0) {
            writeChunk(line, static_cast<std::size_t>(length));
        }
    }
}

bool sdLogIsOpen() {
    return g_open;
}

} // namespace bivouac::log
