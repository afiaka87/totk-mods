// SPDX-License-Identifier: MIT
#if SURVEY_LOCALIZED
#include <lib.hpp>
#include <nn/fs.h>
#include <nn/oe.h>
#include <nn/util.h>
#include <atomic>
#include "Locale.hpp"
#include "ActorCatalog.hpp"
#include "SurveyGameProfiles.hpp"

namespace zonai_survey::locale {
namespace {
alignas(4096) unsigned char g_storage[kArenaBytes]{};
Pack g_pack;
enum LoadState : unsigned { Unloaded, Loading, Ready, Failed };
std::atomic<unsigned> g_state{};

bool read(const char* path, void* output, unsigned capacity, unsigned& size) {
    nn::fs::FileHandle file{};
    if (!nn::fs::OpenFile(&file, path, nn::fs::OpenMode_Read).IsSuccess()) return false;
    long length = 0;
    bool ok = nn::fs::GetFileSize(&length, file).IsSuccess() && length > 0 && length <= long(capacity);
    if (ok) ok = nn::fs::ReadFile(file, 0, output, length).IsSuccess();
    nn::fs::CloseFile(file);
    if (ok) size = unsigned(length);
    return ok;
}

bool load(const char* product, const char* language) {
    char path[96];
    unsigned size = 0;
    nn::util::SNPrintf(path, sizeof(path), "content:/ZonaiSurvey/Locale/%s/%s.bin", product, language);
    return read(path, g_storage, sizeof(g_storage), size) &&
           g_pack.validate(g_storage, size, kCatalogCrc, language);
}

const char* productForVersion(const char* version) {
    return std::strcmp(version, "1.4.0") == 0 ? "140" :
           std::strcmp(version, "1.4.1") == 0 ? "141" :
           (std::strcmp(version, "1.4.2") == 0 || std::strcmp(version, "1.4.3") == 0) ? "142" : "121";
}
}

void loadOnce() {
    unsigned expected = LoadState::Unloaded;
    if (!g_state.compare_exchange_strong(expected, LoadState::Loading, std::memory_order_acq_rel)) return;

    char mask[256]{};
    unsigned size = 0;
    const auto desired = nn::oe::GetDesiredLanguage();
    const unsigned selected = read("content:/System/RegionLangMask.txt", mask, sizeof(mask), size)
        ? selectedLocale(desiredLocale(desired.code), mask, size) : 16;
    const char* product = productForVersion(profiles::active->version);
    const char* language = selected < 16 ? kLocales[selected] : "USen";
    bool ok = load(product, language);
    if (!ok) {
        language = "USen";
        ok = load("121", language);
    }
    Logging.Log("[survey-locale] requested=%.8s selected=%s ready=%u arena=%u\n", desired.code, language, ok, kArenaBytes);
    g_state.store(ok ? LoadState::Ready : LoadState::Failed, std::memory_order_release);
}

bool ready() { return g_state.load(std::memory_order_acquire) == LoadState::Ready; }
const Pack& active() { return g_pack; }
unsigned char* storage() { return g_storage; }
}
#endif
