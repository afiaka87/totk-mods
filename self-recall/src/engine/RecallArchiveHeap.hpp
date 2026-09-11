#pragma once
#include <cstddef>
#include <cstdint>
#include "RecallStorageProfile.hpp"

namespace self_recall::equipment {
inline constexpr std::size_t kArchiveHeapBytes = pure::kArchiveHeapBytes;
void installHeap(std::uintptr_t mainBase);
void* archiveHeap();
std::size_t contiguousArchiveBytes();
bool archiveOwns(const void* address);
void* allocateArchive(std::size_t bytes);
void freeArchive(void* address);
} // namespace self_recall::equipment
