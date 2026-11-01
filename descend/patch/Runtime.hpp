// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>

struct alignas(16) PatchAnchor {
    std::uintptr_t data;
    std::uint32_t phase;
    std::uint32_t failure;
};
struct PatchChunk { std::uint32_t next, low, high; };
struct PatchCodeSite { std::uintptr_t offset; std::uint32_t first, second; };
static_assert(sizeof(PatchAnchor) == 16 && sizeof(PatchChunk) == 12 && sizeof(PatchCodeSite) == 16);

extern "C" {
extern PatchAnchor patch_anchor;
extern const PatchChunk patch_chain_header;
std::uintptr_t patch_main_base();
void patch_initialize();
void patch_installed();
void patch_status(std::uint32_t status, std::uint32_t detail);
std::uint32_t patch_allocate_region(std::uintptr_t*, std::size_t);
std::uint32_t patch_allocate_pages(std::uintptr_t, std::size_t);
void patch_free_region(std::uintptr_t);
void patch_free_pages(std::uintptr_t, std::size_t);
}

inline constexpr std::size_t kPatchDataBytes = 0x2000;
inline constexpr std::size_t kPatchReservationBytes = 0x10000;
inline constexpr std::uint32_t kPatchTextEnd = 0x2b19a50;
inline constexpr std::uint32_t kPatchDataMagic = 0x44504331;
