// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace nn {
struct Result {
    unsigned code = 0;
    bool IsSuccess() const { return code == 0; }
    bool IsFailure() const { return code != 0; }
    unsigned GetInnerValueForDebug() const { return code; }
};
namespace fs {
struct FileHandle {};
enum { OpenMode_Read = 1, OpenMode_ReadWrite = 3, WriteOptionFlag_Flush = 1 };
struct WriteOption {
    int flags;
    static WriteOption CreateOption(int flags) { return {flags}; }
};
namespace fake {
inline std::array<unsigned char, 48> bytes{};
inline bool exists = true, failWrite = false, failRead = false;
inline long size = 48;
inline unsigned writes = 0, reads = 0, closes = 0, mounts = 0;
inline bool wrongPath = false, flushMissing = false;
inline void checkPath(const char* path) {
    if (std::strcmp(path, "arrowbound:/arrowbound/settings.bin") != 0) wrongPath = true;
}
}
inline Result MountSdCard(const char* name) {
    ++fake::mounts;
    if (std::strcmp(name, "arrowbound") != 0) fake::wrongPath = true;
    return {};
}
inline Result CreateDirectory(const char* path) {
    if (std::strcmp(path, "arrowbound:/arrowbound") != 0) fake::wrongPath = true;
    return {};
}
inline Result OpenFile(FileHandle*, const char* path, int) {
    fake::checkPath(path);
    return {fake::exists ? 0u : 1u};
}
inline Result CreateFile(const char* path, std::uint64_t size) {
    fake::checkPath(path);
    if (fake::exists || size != 48) return {1};
    fake::exists = true;
    fake::size = 48;
    fake::bytes = {};
    return {};
}
inline Result GetFileSize(long* size, FileHandle) { *size = fake::size; return {}; }
inline Result ReadFile(FileHandle, long offset, void* data, std::size_t size) {
    ++fake::reads;
    if (fake::failRead || offset < 0 || static_cast<std::size_t>(offset) + size > fake::bytes.size()) return {1};
    std::memcpy(data, fake::bytes.data() + offset, size);
    return {};
}
inline Result WriteFile(FileHandle, long offset, const void* data, std::size_t size, WriteOption option) {
    ++fake::writes;
    if (option.flags != WriteOptionFlag_Flush) fake::flushMissing = true;
    if (offset < 0 || static_cast<std::size_t>(offset) + size > fake::bytes.size()) return {1};
    std::memcpy(fake::bytes.data() + offset, data, fake::failWrite ? size / 2 : size);
    return {fake::failWrite ? 1u : 0u};
}
inline void CloseFile(FileHandle) { ++fake::closes; }
}
}
