#pragma once
#include <cstdint>
namespace totk::engine {
struct CodeSite { std::uintptr_t offset; std::uint32_t first, second; };
template<class T> bool codeMatches(std::uintptr_t, const T&, const char*, const char*) { return true; }
}
