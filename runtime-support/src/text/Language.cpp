// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "totk/text/Language.hpp"
#include <lib.hpp>
#include <nn/fs.h>
#include <nn/oe.h>
#include <atomic>

namespace totk::text {
namespace {
std::atomic<unsigned> g_language{kNoLanguage};

bool readMask(char *mask, std::size_t capacity, std::size_t &size) {
    nn::fs::FileHandle file{};
    if (!nn::fs::OpenFile(&file, "content:/System/RegionLangMask.txt", nn::fs::OpenMode_Read).IsSuccess())
        return false;
    long length = 0;
    bool ok = nn::fs::GetFileSize(&length, file).IsSuccess() && length > 0;
    if (ok) {
        size = std::size_t(length) < capacity ? std::size_t(length) : capacity;
        ok = nn::fs::ReadFile(file, 0, mask, size).IsSuccess();
    }
    nn::fs::CloseFile(file);
    return ok;
}
}  // namespace

unsigned gameTextLanguage() {
    unsigned language = g_language.load(std::memory_order_acquire);
    if (language < kLanguageCount) return language;
    char mask[256]{};
    std::size_t size = 0;
    if (!readMask(mask, sizeof(mask), size)) return kUsEnglish;
    const auto console = nn::oe::GetDesiredLanguage();
    language = offeredLanguage(consoleLanguage(console.code), mask, size);
    if (language >= kLanguageCount) language = kUsEnglish;
    if (g_language.exchange(language, std::memory_order_acq_rel) != language)
        Logging.Log("[text] TEXT_LANGUAGE console=%.8s game=%s", console.code, kLanguageCodes[language]);
    return language;
}
}  // namespace totk::text
