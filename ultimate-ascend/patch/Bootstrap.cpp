// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Runtime.hpp"
#include "Decode.hpp"

namespace {
const PatchChunk* nextChunk(std::uintptr_t base, const PatchChunk* chunk) {
    const auto offset = chunk->next;
    if (!offset || (offset & 3) || offset > kPatchTextEnd - sizeof(PatchChunk)) return nullptr;
    return reinterpret_cast<const PatchChunk*>(base + offset);
}

struct ChunkReader {
    std::uintptr_t base;
    std::uint32_t offset;
    std::size_t left;
    std::size_t remaining() const { return left; }
    bool operator()(std::uint8_t& byte) {
        if (!left || !offset || (offset & 3) || offset > kPatchTextEnd - 4) return false;
        // One byte and a 24-bit word address fit a four-byte alignment gap.
        const auto word = *reinterpret_cast<const std::uint32_t*>(base + offset);
        byte = static_cast<std::uint8_t>(word);
        offset = (word >> 8) << 2;
        --left;
        return true;
    }
    bool ended() const { return !left && !offset; }
};

// Our original-code stubs retain the first 20 bytes. A later absolute hook can therefore keep its tail at the native entry without changing our continuation.
bool absoluteTail(std::uintptr_t site, std::uint32_t first) {
    if ((first & 0xfc000000u) != 0x14000000u) return false;
    const auto* words = reinterpret_cast<const std::uint32_t*>(site);
    const bool unaligned = (site & 7) == 4;
    if (unaligned ? (words[1] != 0x58000051u || words[2] != 0xd61f0220u)
                  : words[1] != 0xd61f0220u) return false;
    const auto oldTarget = *reinterpret_cast<const std::uintptr_t*>(site + (unaligned ? 12 : 8));
    const auto immediate = std::int64_t(first & 0x03ffffffu);
    const auto displacement = (immediate >= 0x02000000 ? immediate - 0x04000000 : immediate) * 4;
    const auto newTarget = site + displacement;
    // Neither target is dereferenced or called here. The other module owns them.
    const std::uintptr_t targets[]{oldTarget, newTarget};
    for (const auto target : targets) {
        if (target < 0x10000 || (target & 3) || (target >= site && target < site + 20)) return false;
    }
    return true;
}

std::uint32_t badCode(std::uintptr_t base, const PatchCodeSite* sites, std::uint32_t count) {
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& site = sites[index];
        if ((site.offset & 3) || site.offset > kPatchTextEnd - 8) return 0xffffffffu;
        const auto* words = reinterpret_cast<const std::uint32_t*>(base + site.offset);
        if ((words[0] != site.first && (words[0] & 0xfc000000u) != 0x14000000u) ||
            (site.second && words[1] != site.second && !absoluteTail(base + site.offset, words[0])))
            return static_cast<std::uint32_t>(site.offset);
    }
    return 0;
}

void failed(std::uint32_t code, std::uint32_t detail, std::uintptr_t region = 0, bool mapped = false) {
    if (mapped) patch_free_pages(region, kPatchDataBytes);
    if (region) patch_free_region(region);
    patch_anchor.failure = code;
    __atomic_store_n(&patch_anchor.phase, 3, __ATOMIC_RELEASE);
    patch_status(code, detail);
}
}

extern "C" void patch_initialize() {
    std::uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&patch_anchor.phase, &expected, 1, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    const auto base = patch_main_base();
    const auto* header = &patch_chain_header;
    const auto dataBytes = header->low;
    const auto baseOffset = header->high;
    const auto* guards = nextChunk(base, header);
    const auto* integrity = guards ? nextChunk(base, guards) : nullptr;
    const auto* encoding = integrity ? nextChunk(base, integrity) : nullptr;
    if (!encoding || integrity->high != kPatchDataMagic || !dataBytes || dataBytes > kPatchDataBytes ||
        !encoding->low || encoding->low > kPatchDataBytes + 64 || encoding->high != 1 ||
        (baseOffset & 7) || baseOffset + 8ull > dataBytes || !guards->high || guards->high > 128 ||
        (guards->low & 7) || guards->low + std::uint64_t(guards->high) * sizeof(PatchCodeSite) > dataBytes) {
        failed(1, dataBytes);
        return;
    }
    std::uintptr_t region = 0;
    const auto reserved = patch_allocate_region(&region, kPatchReservationBytes);
    if (reserved) { failed(2, reserved); return; }
    const auto mapped = patch_allocate_pages(region, kPatchDataBytes);
    if (mapped) { failed(3, mapped, region); return; }
    auto* output = reinterpret_cast<std::uint8_t*>(region);
    for (std::size_t i = 0; i < kPatchDataBytes; ++i) output[i] = 0;
    ChunkReader reader{base, encoding->next, encoding->low};
    if (!decodeBlock(reader,output,dataBytes) || !reader.ended()) {
        failed(4, dataBytes, region, true); return;
    }
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < dataBytes; ++i) hash = (hash ^ output[i]) * 16777619u;
    if (hash != integrity->low) { failed(5, hash, region, true); return; }
    const auto mismatch = badCode(base, reinterpret_cast<const PatchCodeSite*>(output + guards->low), guards->high);
    if (mismatch) { failed(6, mismatch, region, true); return; }
    *reinterpret_cast<std::uintptr_t*>(output + baseOffset) = base;
    __atomic_store_n(&patch_anchor.data, region, __ATOMIC_RELEASE);
    __atomic_store_n(&patch_anchor.phase, 2, __ATOMIC_RELEASE);
    patch_status(0, dataBytes);
    patch_installed();
}
