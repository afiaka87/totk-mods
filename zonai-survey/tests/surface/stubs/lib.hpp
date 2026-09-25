// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace trace_test {
enum class Failure { None, Mount, Create, Open, Write, Flush };
struct Io {
    Failure failure{};
    unsigned mounts{}, creates{}, opens{}, writes{}, flushes{}, closes{}, stops{};
    std::uint64_t size{}, end{}, tick{};
    bool overflow{};
};
inline Io io;
}
inline std::uint64_t svcGetSystemTick() { return ++trace_test::io.tick; }
struct TestLogger {
    template<class... T> void Log(const char* format, T...) {
        if (std::strstr(format, "TRACE_STOPPED")) ++trace_test::io.stops;
    }
};
inline TestLogger Logging;
namespace nn {
struct Result {
    unsigned code{};
    bool IsSuccess() const { return code == 0; }
    bool IsFailure() const { return code != 0; }
    unsigned GetInnerValueForDebug() const { return code; }
};
namespace util {
template<class... T> int SNPrintf(char* buffer, std::size_t size, const char* format, T... args) {
    return std::snprintf(buffer, size, format, args...);
}
}
namespace fs {
struct FileHandle { unsigned value{}; };
struct WriteOption { unsigned flags{}; };
inline constexpr unsigned OpenMode_Write = 2;
inline Result result(trace_test::Failure failure) { return {trace_test::io.failure == failure ? 7u : 0u}; }
inline Result MountSdCard(const char*) { ++trace_test::io.mounts; return result(trace_test::Failure::Mount); }
inline Result CreateFile(const char*, std::uint64_t size) {
    ++trace_test::io.creates; trace_test::io.size = size; return result(trace_test::Failure::Create);
}
inline Result OpenFile(FileHandle*, const char*, unsigned) { ++trace_test::io.opens; return result(trace_test::Failure::Open); }
inline Result WriteFile(FileHandle, std::uint64_t offset, const void*, std::size_t bytes, WriteOption) {
    ++trace_test::io.writes;
    trace_test::io.end = offset+bytes;
    if (trace_test::io.end > trace_test::io.size) trace_test::io.overflow = true;
    return result(trace_test::Failure::Write);
}
inline Result FlushFile(FileHandle) { ++trace_test::io.flushes; return result(trace_test::Failure::Flush); }
inline void CloseFile(FileHandle) { ++trace_test::io.closes; }
}
}
