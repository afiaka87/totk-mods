// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstddef>
#include <cstring>

// The language the game shows its text in, which every mod's own text follows. The game takes the
// console's language when its RegionLangMask.txt offers it, else one from the same region.
namespace totk::text {
inline constexpr unsigned kLanguageCount = 16;
inline constexpr unsigned kNoLanguage = kLanguageCount;
inline constexpr unsigned kUsEnglish = 1;
// The game's names for its text languages. EUpt is in the game's list but no TotK version offers it.
inline constexpr const char *kLanguageCodes[kLanguageCount]{"JPja", "USen", "USes", "USfr", "USpt", "EUen",
                                                            "EUes", "EUfr", "EUde", "EUit", "EUpt", "EUnl",
                                                            "EUru", "KRko", "CNzh", "TWzh"};
// The console language setting each of them stands for.
inline constexpr const char *kConsoleCodes[kLanguageCount]{"ja", "en-US", "es-419", "fr-CA", "pt-BR", "en-GB",
                                                           "es", "fr", "de", "it", "pt", "nl",
                                                           "ru", "ko", "zh-Hans", "zh-Hant"};

inline unsigned consoleLanguage(const char *code) {
    for (unsigned i = 0; code && i < kLanguageCount; ++i)
        if (std::strncmp(code, kConsoleCodes[i], 8) == 0) return i;
    return 0;  // The game's own fallback for a code it does not know.
}

inline unsigned region(unsigned language) {
    return language == 0 ? 0 : language <= 4 ? 1 : language <= 12 ? 2 : language - 10;
}

// The first line of RegionLangMask.txt lists the text languages, whatever the voice setting.
inline unsigned offeredLanguage(unsigned wanted, const char *mask, std::size_t size) {
    unsigned offered = 0;
    for (std::size_t at = 0; at < size && mask[at] != '\n' && mask[at] != '\r';) {
        if (mask[at] == ' ' || mask[at] == '\t') {
            ++at;
            continue;
        }
        std::size_t end = at;
        while (end < size && mask[end] != ' ' && mask[end] != '\t' && mask[end] != '\r' && mask[end] != '\n') ++end;
        if (end - at == 4)
            for (unsigned i = 0; i < kLanguageCount; ++i)
                if (!std::memcmp(mask + at, kLanguageCodes[i], 4)) offered |= 1u << i;
        at = end;
    }
    if (!offered) return kNoLanguage;
    if (wanted < kLanguageCount && (offered & (1u << wanted))) return wanted;
    for (unsigned i = 0; i < kLanguageCount; ++i)
        if ((offered & (1u << i)) && region(i) == region(wanted)) return i;
    for (unsigned i = 0; i < kLanguageCount; ++i)
        if (offered & (1u << i)) return i;
    return kNoLanguage;
}

// The game's text language (src/text/Language.cpp). US English until the game's files can be read, which
// is retried on the next call; the choice is made once and logged.
unsigned gameTextLanguage();
}  // namespace totk::text
