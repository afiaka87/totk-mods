// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
using s64 = std::int64_t;
namespace nn {
struct Result {
    unsigned value{};
    bool IsSuccess() const { return value == 0; }
    unsigned GetInnerValueForDebug() const { return value; }
};
namespace fs {
struct FileHandle { std::uint64_t value{}; };
constexpr int OpenMode_Read = 1;
Result OpenFile(FileHandle*, const char*, int);
Result GetFileSize(s64*, FileHandle);
Result ReadFile(FileHandle, s64, void*, std::uint64_t);
void CloseFile(FileHandle);
}
}
