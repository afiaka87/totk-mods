#pragma once

#include <cstddef>
#include <cstdint>

namespace totk::engine::read_guard {

#if defined(EXL_LOAD_KIND)
// Logs the calling site once; defined in RecallReadGuard.cpp.
[[gnu::noinline]] void reject(std::uintptr_t address, std::size_t size);

// Hardware faults outside the 39-bit user address space, where Eden reads zero; refuse and read zero too.
[[nodiscard]] inline bool admit(const void* base, std::size_t offset, std::size_t size) {
    const auto address = reinterpret_cast<std::uintptr_t>(base) + offset;
    if (address >= 0x1000 && ((address + size - 1) >> 39) == 0) return true;
    reject(address, size);
    return false;
}
#else
[[nodiscard]] inline bool admit(const void*, std::size_t, std::size_t) { return true; }
#endif

}
