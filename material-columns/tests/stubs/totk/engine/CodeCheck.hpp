#pragma once
#include <cstdint>
namespace totk::engine {
struct CodeSite { std::uintptr_t offset; std::uint32_t first, second; };
template<class T> bool codeMatches(std::uintptr_t base, const T& sites, const char*, const char*) {
#ifdef MATERIAL_COLUMNS_VERIFY_CODE_WORDS
    for (const auto& site : sites) {
        const auto* words=reinterpret_cast<const std::uint32_t*>(base+site.offset);
        if (words[0]!=site.first || (site.second && words[1]!=site.second)) return false;
    }
#else
    (void)base; (void)sites;
#endif
    return true;
}
}
