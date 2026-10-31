// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Localization.hpp"
#include "../pure/Localization.hpp"
#include <lib.hpp>
#include <nn/fs.h>
#include <nn/oe.h>
#include <cstdio>

// Verified SDK result/output ABI on 1.2.1 and 1.4.2; bypass application-language filtering.
namespace nn::settings::detail { nn::Result GetLanguageCode(nn::settings::LanguageCode*); }

namespace material_columns::native::locale {
namespace {
unsigned char storage[localization::capacity]{};
localization::Pack pack;
bool attempted{};
const char* systemLanguage="en-US";
bool load(const char* language) {
    char path[96];
    std::snprintf(path, sizeof(path), "content:/MaterialColumns/Locale/%s.bin", language);
    nn::fs::FileHandle file{};
    if (!nn::fs::OpenFile(&file, path, nn::fs::OpenMode_Read).IsSuccess()) return false;
    s64 size=0;
    const bool read = nn::fs::GetFileSize(&size,file).IsSuccess() && size>0 && size<=s64(sizeof(storage)) &&
        nn::fs::ReadFile(file,0,storage,size).IsSuccess();
    nn::fs::CloseFile(file);
    return read && pack.load(storage,static_cast<std::size_t>(size));
}
}
void loadOnce() {
    if (attempted) return;
    attempted = true;
    nn::settings::LanguageCode desired{};
    const bool system = nn::settings::detail::GetLanguageCode(&desired).IsSuccess();
    if (!system) desired = nn::oe::GetDesiredLanguage();
    const char* selected = localization::select(desired.code);
    systemLanguage=selected;
    bool loaded = load(selected);
    if (!loaded && std::strcmp(selected,"en-US")) { selected="en-US"; loaded=load(selected); }
    Logging.Log("[material-columns] locale requested=%.8s selected=%s system=%u loaded=%u buffer=%u",
        desired.code, selected, unsigned(system), unsigned(loaded), unsigned(sizeof(storage)));
}
const char* label(std::uint64_t group, const char* fallback) { return pack.label(group,fallback); }
const char* language() { return systemLanguage; }
}
