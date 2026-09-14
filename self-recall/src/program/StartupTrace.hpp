#pragma once

#include <cstdint>

namespace self_recall::startup_trace {

#if SELF_RECALL_ROMFS_DIAGNOSTIC

void begin();
void mark(const char* stage, std::uint64_t value0 = 0, std::uint64_t value1 = 0);
[[nodiscard]] bool ready();
[[nodiscard]] const char* path();

#else

inline void begin() {}
inline void mark(const char*, std::uint64_t = 0, std::uint64_t = 0) {}
[[nodiscard]] inline bool ready() { return false; }
[[nodiscard]] inline const char* path() { return "disabled"; }

#endif

}
