// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <lib.hpp>
#include <nvn/nvn.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace totk::render {
// Each pass clears shared glow storage and finishes sampling before the next pass reuses it.
enum GlowImage : unsigned { Bright, NarrowA, NarrowB, WideA, WideB, kGlowTargets };
inline constexpr unsigned kGlowSceneWidth = 1600, kGlowSceneHeight = 900, kGlowDivisor = 2;
inline constexpr unsigned kGlowWidth = 800, kGlowHeight = 450;
// Bright needs 1,638,400 bytes; scratch retains 3,276,800 for full-resolution bomb depth.
inline constexpr std::size_t kGlowPoolBytes = 4915200;

struct GlowTargets {
    NVNmemoryPool* pool{};
    std::size_t scratchOffset{}, scratchBytes{};
    NVNtexture images[kGlowTargets];
    NVNtextureView views[kGlowTargets];
    int ids[kGlowTargets];
    std::size_t bytes;
};

using RegisterTexture = int (*)(void* manager, NVNtexture*, NVNtextureView*);

namespace detail {
alignas(65536) inline unsigned char g_glowMemory[kGlowPoolBytes]{};
alignas(8) inline NVNmemoryPool g_glowPool{};
alignas(8) inline GlowTargets g_glow{};
inline bool g_glowAttempted{}, g_glowReady{};
template <class T> T load(std::uintptr_t address) {
    T value{};
    if (address) std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}
}  // namespace detail

// Initialization is attempted once; a failed pool remains unavailable.
inline const GlowTargets* glowTargets(NVNdevice* device, PFNNVNDEVICEGETPROCADDRESSPROC getProc,
                                      void* manager, RegisterTexture registerTexture,
                                      const char* owner) {
    using namespace detail;
    if (g_glowAttempted) return g_glowReady ? &g_glow : nullptr;
    g_glowAttempted = true;
    const auto fail = [&](const char* step, unsigned value) -> const GlowTargets* {
        Logging.Log("[%s] GLOW_TARGETS_FAILED step=%s detail=%u", owner, step, value);
        return nullptr;
    };
    const auto resolve = [&](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
            getProc(device, name));
        return function != nullptr;
    };
    PFNNVNMEMORYPOOLBUILDERSETDEFAULTSPROC poolDefaults{};
    PFNNVNMEMORYPOOLBUILDERSETDEVICEPROC poolDevice{};
    PFNNVNMEMORYPOOLBUILDERSETSTORAGEPROC poolStorage{};
    PFNNVNMEMORYPOOLBUILDERSETFLAGSPROC poolFlags{};
    PFNNVNMEMORYPOOLINITIALIZEPROC poolInit{};
    PFNNVNTEXTUREBUILDERSETDEFAULTSPROC texDefaults{};
    PFNNVNTEXTUREBUILDERSETDEVICEPROC texDevice{};
    PFNNVNTEXTUREBUILDERSETTARGETPROC texTarget{};
    PFNNVNTEXTUREBUILDERSETSIZE2DPROC texSize{};
    PFNNVNTEXTUREBUILDERSETFORMATPROC texFormat{};
    PFNNVNTEXTUREBUILDERGETSTORAGESIZEPROC texBytes{};
    PFNNVNTEXTUREBUILDERGETSTORAGEALIGNMENTPROC texAlignment{};
    PFNNVNTEXTUREBUILDERSETSTORAGEPROC texStorage{};
    PFNNVNTEXTUREINITIALIZEPROC texInit{};
    PFNNVNTEXTUREVIEWSETDEFAULTSPROC viewDefaults{};
    if (!device || !getProc || !manager || !registerTexture ||
        !resolve(poolDefaults, "nvnMemoryPoolBuilderSetDefaults") ||
        !resolve(poolDevice, "nvnMemoryPoolBuilderSetDevice") ||
        !resolve(poolStorage, "nvnMemoryPoolBuilderSetStorage") ||
        !resolve(poolFlags, "nvnMemoryPoolBuilderSetFlags") ||
        !resolve(poolInit, "nvnMemoryPoolInitialize") ||
        !resolve(texDefaults, "nvnTextureBuilderSetDefaults") ||
        !resolve(texDevice, "nvnTextureBuilderSetDevice") ||
        !resolve(texTarget, "nvnTextureBuilderSetTarget") ||
        !resolve(texSize, "nvnTextureBuilderSetSize2D") ||
        !resolve(texFormat, "nvnTextureBuilderSetFormat") ||
        !resolve(texBytes, "nvnTextureBuilderGetStorageSize") ||
        !resolve(texAlignment, "nvnTextureBuilderGetStorageAlignment") ||
        !resolve(texStorage, "nvnTextureBuilderSetStorage") ||
        !resolve(texInit, "nvnTextureInitialize") ||
        !resolve(viewDefaults, "nvnTextureViewSetDefaults"))
        return fail("functions", 0);
    alignas(8) NVNmemoryPoolBuilder pool{};
    poolDefaults(&pool);
    poolDevice(&pool, device);
    poolStorage(&pool, g_glowMemory, sizeof(g_glowMemory));
    poolFlags(&pool, NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS | NVN_MEMORY_POOL_FLAGS_GPU_CACHED |
                     NVN_MEMORY_POOL_FLAGS_COMPRESSIBLE);
    if (!poolInit(&g_glowPool, &pool)) return fail("pool", unsigned(sizeof(g_glowMemory)));
    // Bright and NarrowA have their own storage; the later images are placed inside Bright's.
    std::size_t used = 0, brightEnd = 0, aliased = 0;
    for (unsigned i = 0; i < kGlowTargets; ++i) {
        alignas(8) NVNtextureBuilder texture{};
        texDefaults(&texture);
        texDevice(&texture, device);
        texTarget(&texture, NVN_TEXTURE_TARGET_2D);
        texFormat(&texture, i == Bright ? NVN_FORMAT_R11G11B10F : NVN_FORMAT_RGBA16F);
        const unsigned divisor = i == Bright ? 1 : i < WideA ? 2 : 4;
        texSize(&texture, (kGlowWidth + divisor - 1) / divisor,
                (kGlowHeight + divisor - 1) / divisor);
        const auto alignment = texAlignment(&texture), bytes = texBytes(&texture);
        if (!alignment || (alignment & (alignment - 1)) || !bytes) return fail("layout", i);
        const bool shared = i >= NarrowB;
        auto& cursor = shared ? aliased : used;
        cursor = (cursor + alignment - 1) & ~std::size_t(alignment - 1);
        const std::size_t limit = shared ? brightEnd : kGlowPoolBytes;
        if (cursor > limit || bytes > limit - cursor)
            return fail(shared ? "shared" : "capacity", unsigned(cursor + bytes));
        texStorage(&texture, &g_glowPool, cursor);
        if (i == NarrowA) {
            g_glow.scratchOffset = cursor;
            g_glow.scratchBytes = kGlowPoolBytes - cursor;
        }
        cursor += bytes;
        if (i == Bright) brightEnd = used;
        if (!texInit(&g_glow.images[i], &texture)) return fail("texture", i);
        viewDefaults(&g_glow.views[i]);
        g_glow.ids[i] = registerTexture(manager, &g_glow.images[i], nullptr);
        if (g_glow.ids[i] < 0) return fail("descriptor", i);
    }
    g_glow.bytes = used;
    g_glow.pool = &g_glowPool;
    g_glowReady = true;
    Logging.Log("[%s] GLOW_TARGETS_READY bytes=%u shared=%u pool=%u", owner, unsigned(used),
                unsigned(aliased), unsigned(kGlowPoolBytes));
    return &g_glow;
}

// The game's colour target for a model render context, as bound by its default framebuffer.
struct GameFrame {
    NVNtexture* texture;
    const NVNtextureView* view;
};

// Resolve colour target zero through the same registry lookup as the native framebuffer bind.
inline bool gameFrame(std::uintptr_t moduleBase, void* context, GameFrame& out) {
    using detail::load;
    const auto address = reinterpret_cast<std::uintptr_t>(context);
    const auto buffer = load<std::uintptr_t>(address + 0x540);
    const auto target = load<std::uintptr_t>(buffer + 0x20);
    const auto manager = load<std::uintptr_t>(load<std::uintptr_t>(moduleBase + 0x462EF90));
    if (!buffer || !target || !manager) return false;
    auto index = load<std::uint32_t>(target + 96) - load<std::uint16_t>(manager + 1442);
    if (index >= load<std::uint32_t>(manager + 1328)) index = 0;
    const auto table = load<std::uintptr_t>(manager + 1336);
    out.texture = table ? load<NVNtexture*>(table + 32 * std::uintptr_t(index) + 24) : nullptr;
    out.view = reinterpret_cast<const NVNtextureView*>(target + 184);
    return out.texture != nullptr;
}
}  // namespace totk::render
