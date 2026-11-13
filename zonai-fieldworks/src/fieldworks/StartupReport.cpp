// Copyright (c) HanSoloBlaster, afiaka87
#include "StartupReport.hpp"

#if FIELDWORKS_SD_REPORT

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <lib.hpp>
#include <nn/fs.h>

#include "totk/engine/SdCardMount.hpp"

extern "C" Result __real_svcOutputDebugString(const char* text, u64 size);

namespace fieldworks::report {
namespace {

constexpr char kDirectory[] = "sdcard:/ZonaiFieldworks";
constexpr std::uint32_t kPathNotFound = 0x202, kPathAlreadyExists = 0x402;  // fs module 2
constexpr std::size_t kCapacity = 64 * 1024;
constexpr unsigned kWriteTicks = 300, kTries = 20;

char g_text[kCapacity];
std::size_t g_used{};     // reserved by the threads that log; text below it may still be copying
bool g_full{};            // a line did not fit: nothing more is kept
std::size_t g_written{};  // controller thread only
unsigned g_failures{};    // controller thread only

// Log lines come from several threads. Each reserves its bytes first; a line that does not fit ends the
// report, so the file is the start of the session and stays one size.
void keep(const char* text, std::size_t size) {
    if (!text || !size || __atomic_load_n(&g_full, __ATOMIC_RELAXED)) return;
    std::size_t at = __atomic_load_n(&g_used, __ATOMIC_RELAXED);
    do {
        if (at + size + 1 > kCapacity) {
            __atomic_store_n(&g_full, true, __ATOMIC_RELAXED);
            return;
        }
    } while (!__atomic_compare_exchange_n(&g_used, &at, at + size + 1, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
    std::memcpy(g_text + at, text, size);
    g_text[at + size] = '\n';
}

// The whole text is written again each time, so a line that was still copying last time is complete now.
bool write(std::size_t size) {
    if (totk::engine::ensureSdCardMounted() == totk::engine::SdCardMountResult::Failed) return false;
    auto result = nn::fs::CreateDirectory(kDirectory);
    if (result.IsFailure() && result.GetInnerValueForDebug() != kPathAlreadyExists) return false;
    nn::fs::FileHandle file;
    result = nn::fs::OpenFile(&file, kFile, nn::fs::OpenMode_Write);
    if (result.IsFailure() && result.GetInnerValueForDebug() == kPathNotFound) {
        result = nn::fs::CreateFile(kFile, static_cast<s64>(size));
        if (!result.IsFailure()) result = nn::fs::OpenFile(&file, kFile, nn::fs::OpenMode_Write);
    }
    if (result.IsFailure()) return false;
    result = nn::fs::SetFileSize(file, static_cast<s64>(size));
    if (!result.IsFailure()) {
        const nn::fs::WriteOption flush{nn::fs::WriteOptionFlag_Flush};
        result = nn::fs::WriteFile(file, 0, g_text, static_cast<u64>(size), flush);
    }
    nn::fs::CloseFile(file);
    return !result.IsFailure();
}

}  // namespace

void tick(unsigned now) {
    if (now != 1 && now % kWriteTicks != 0) return;
    const std::size_t size = __atomic_load_n(&g_used, __ATOMIC_ACQUIRE);
    if (size == g_written || g_failures >= kTries) return;
    if (write(size)) {
        g_written = size;
    } else {
        ++g_failures;
    }
}

}  // namespace fieldworks::report

// The linker sends every debug-output call in the module here (CMakeLists.txt, --wrap).
extern "C" Result __wrap_svcOutputDebugString(const char* text, u64 size) {
    fieldworks::report::keep(text, static_cast<std::size_t>(size));
    return __real_svcOutputDebugString(text, size);
}

#endif  // FIELDWORKS_SD_REPORT
