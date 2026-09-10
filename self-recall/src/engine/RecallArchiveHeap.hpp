#pragma once
#include <cstddef>
#include <cstdint>

namespace self_recall::equipment {
inline constexpr std::size_t kArchiveHeapBytes = 64u * 1024u * 1024u;
void installHeap(std::uintptr_t mainBase);
void* archiveHeap();
std::size_t contiguousArchiveBytes();
bool archiveOwns(const void* address);
} // namespace self_recall::equipment
