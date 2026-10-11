// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombTargets.hpp"
#include <lib.hpp>
#include <cstring>
#include <type_traits>

namespace remote_bomb::targets {
namespace {
constexpr std::size_t kPoolBytes = 64 * 1024 * 1024;
// The depth buffer gets its own compressible pool: the game's framebuffers and every agl depth
// target are compressible, and an uncompressed depth texture fails to initialize.
constexpr std::size_t kDepthBytes = 12 * 1024 * 1024, kColorBytes = kPoolBytes - kDepthBytes;
alignas(65536) unsigned char memory[kPoolBytes]{};
NVNmemoryPool pool{}, depthPool{};
bool depthReady{};
NVNtexture images[Count]{};
int ids[Count]{};
NVNdevice *device{};
NVNdepthStencilState depthOn{}, depthOff{}, depthRead{};
PFNNVNCOMMANDBUFFERSETRENDERTARGETSPROC setTargets{};
PFNNVNCOMMANDBUFFERSETVIEWPORTPROC viewport{};
PFNNVNCOMMANDBUFFERSETSCISSORPROC scissor{};
PFNNVNCOMMANDBUFFERCLEARCOLORPROC clearColor{};
PFNNVNCOMMANDBUFFERCLEARDEPTHSTENCILPROC clearDepth{};
PFNNVNCOMMANDBUFFERBINDDEPTHSTENCILSTATEPROC bindDepth{};
PFNNVNDEVICEGETTEXTUREHANDLEPROC textureHandle{};
template <class T> T read(std::uintptr_t address) {
    T value{};
    if (address) std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(value));
    return value;
}
}
bool initialize(NVNdevice *gpu, PFNNVNDEVICEGETPROCADDRESSPROC getProc, std::uintptr_t base) {
    device = gpu;
    const auto resolve = [&](auto &function, const char *name) {
        using F = std::remove_reference_t<decltype(function)>;
        function = reinterpret_cast<F>(getProc(device, name));
        if (!function) Logging.Log("[remote-bomb] TARGET_FUNCTION_MISSING name=%s", name);
        return function != nullptr;
    };
    PFNNVNMEMORYPOOLBUILDERSETDEFAULTSPROC poolDefaults{};
    PFNNVNMEMORYPOOLBUILDERSETDEVICEPROC poolDevice{};
    PFNNVNMEMORYPOOLBUILDERSETSTORAGEPROC poolStorage{};
    PFNNVNMEMORYPOOLBUILDERSETFLAGSPROC poolFlags{};
    PFNNVNMEMORYPOOLINITIALIZEPROC poolInit{};
    PFNNVNTEXTUREBUILDERSETDEFAULTSPROC defaults{};
    PFNNVNTEXTUREBUILDERSETDEVICEPROC setDevice{};
    PFNNVNTEXTUREBUILDERSETTARGETPROC target{};
    PFNNVNTEXTUREBUILDERSETSIZE2DPROC size{};
    PFNNVNTEXTUREBUILDERSETFORMATPROC format{};
    PFNNVNTEXTUREBUILDERSETFLAGSPROC flags{};
    PFNNVNTEXTUREBUILDERGETSTORAGESIZEPROC bytes{};
    PFNNVNTEXTUREBUILDERGETSTORAGEALIGNMENTPROC alignment{};
    PFNNVNTEXTUREBUILDERSETSTORAGEPROC storage{};
    PFNNVNTEXTUREINITIALIZEPROC init{};
    PFNNVNDEPTHSTENCILSTATESETDEFAULTSPROC stateDefaults{};
    PFNNVNDEPTHSTENCILSTATESETDEPTHTESTENABLEPROC test{};
    PFNNVNDEPTHSTENCILSTATESETDEPTHWRITEENABLEPROC write{};
    PFNNVNDEPTHSTENCILSTATESETDEPTHFUNCPROC compare{};
    if (!resolve(poolDefaults, "nvnMemoryPoolBuilderSetDefaults") ||
        !resolve(poolDevice, "nvnMemoryPoolBuilderSetDevice") ||
        !resolve(poolStorage, "nvnMemoryPoolBuilderSetStorage") ||
        !resolve(poolFlags, "nvnMemoryPoolBuilderSetFlags") ||
        !resolve(poolInit, "nvnMemoryPoolInitialize") ||
        !resolve(defaults, "nvnTextureBuilderSetDefaults") ||
        !resolve(setDevice, "nvnTextureBuilderSetDevice") ||
        !resolve(target, "nvnTextureBuilderSetTarget") ||
        !resolve(size, "nvnTextureBuilderSetSize2D") ||
        !resolve(format, "nvnTextureBuilderSetFormat") ||
        !resolve(flags, "nvnTextureBuilderSetFlags") ||
        !resolve(bytes, "nvnTextureBuilderGetStorageSize") ||
        !resolve(alignment, "nvnTextureBuilderGetStorageAlignment") ||
        !resolve(storage, "nvnTextureBuilderSetStorage") ||
        !resolve(init, "nvnTextureInitialize") ||
        !resolve(stateDefaults, "nvnDepthStencilStateSetDefaults") ||
        !resolve(test, "nvnDepthStencilStateSetDepthTestEnable") ||
        !resolve(write, "nvnDepthStencilStateSetDepthWriteEnable") ||
        !resolve(compare, "nvnDepthStencilStateSetDepthFunc") ||
        !resolve(bindDepth, "nvnCommandBufferBindDepthStencilState") ||
        !resolve(setTargets, "nvnCommandBufferSetRenderTargets") ||
        !resolve(viewport, "nvnCommandBufferSetViewport") ||
        !resolve(scissor, "nvnCommandBufferSetScissor") ||
        !resolve(clearColor, "nvnCommandBufferClearColor") ||
        !resolve(clearDepth, "nvnCommandBufferClearDepthStencil") ||
        !resolve(textureHandle, "nvnDeviceGetTextureHandle")) return false;
    NVNmemoryPoolBuilder builder{};
    poolDefaults(&builder); poolDevice(&builder, device); poolStorage(&builder, memory, kColorBytes);
    poolFlags(&builder, NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS | NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    if (!poolInit(&pool, &builder)) {
        Logging.Log("[remote-bomb] TARGET_POOL_FAILED bytes=%u", unsigned(kColorBytes));
        return false;
    }
    NVNmemoryPoolBuilder depthBuilder{};
    poolDefaults(&depthBuilder); poolDevice(&depthBuilder, device);
    poolStorage(&depthBuilder, memory + kColorBytes, kDepthBytes);
    poolFlags(&depthBuilder, NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS | NVN_MEMORY_POOL_FLAGS_GPU_CACHED |
                                 NVN_MEMORY_POOL_FLAGS_COMPRESSIBLE);
    const bool depthPoolReady = poolInit(&depthPool, &depthBuilder);
    if (!depthPoolReady)
        Logging.Log("[remote-bomb] TARGET_DEPTH_POOL_FAILED bytes=%u", unsigned(kDepthBytes));
    const auto manager = read<void *>(read<std::uintptr_t>(base + 0x462EF90));
    if (!manager) {
        Logging.Log("[remote-bomb] TARGET_MANAGER_MISSING");
        return false;
    }
    std::size_t used{};
    for (unsigned i = 0; i < Count; ++i) {
        const bool depth = i == Depth;
        if (depth && !depthPoolReady)
            break;
        NVNtextureBuilder texture{};
        defaults(&texture); setDevice(&texture, device); target(&texture, NVN_TEXTURE_TARGET_2D);
        const unsigned divisor = depth || i < NarrowA ? 1 : i < WideA ? 2 : 4;
        size(&texture, (kWidth+divisor-1)/divisor, (kHeight+divisor-1)/divisor);
        format(&texture, depth ? NVN_FORMAT_DEPTH32F : NVN_FORMAT_RGBA16F);
        const int textureFlags = depth ? NVN_TEXTURE_FLAGS_COMPRESSIBLE : 0;
        flags(&texture, textureFlags);
        const std::size_t capacity = depth ? kDepthBytes : kColorBytes;
        std::size_t offset = depth ? 0 : used;
        const auto align = alignment(&texture), count = bytes(&texture);
        if (!align || (align & (align-1)) || align > capacity || !count) {
            Logging.Log("[remote-bomb] TARGET_LAYOUT_FAILED image=%u alignment=%u bytes=%u", i, unsigned(align), unsigned(count));
            if (depth) break;
            return false;
        }
        offset = (offset+align-1) & ~(align-1);
        if (offset > capacity || count > capacity-offset) {
            Logging.Log("[remote-bomb] TARGET_CAP_FAILED image=%u used=%u bytes=%u", i, unsigned(offset), unsigned(count));
            if (depth) break;
            return false;
        }
        storage(&texture, depth ? &depthPool : &pool, offset);
        if (!init(&images[i], &texture)) {
            Logging.Log("[remote-bomb] TARGET_TEXTURE_FAILED image=%u flags=%d alignment=%u bytes=%u", i,
                        textureFlags, unsigned(align), unsigned(count));
            if (depth) break;
            return false;
        }
        if (depth) {
            depthReady = true;
            continue;
        }
        used = offset + count;
        ids[i] = reinterpret_cast<int (*)(void *, NVNtexture *, NVNtextureView *)>(base + 0xA5B0B4)(manager, &images[i], nullptr);
        if (ids[i] < 0) {
            Logging.Log("[remote-bomb] TARGET_DESCRIPTOR_FAILED image=%u id=%d", i, ids[i]);
            return false;
        }
    }
    stateDefaults(&depthOn); test(&depthOn, true); write(&depthOn, true); compare(&depthOn, NVN_DEPTH_FUNC_LESS);
    stateDefaults(&depthOff); test(&depthOff, false); write(&depthOff, false);
    stateDefaults(&depthRead); test(&depthRead, true); write(&depthRead, false); compare(&depthRead, NVN_DEPTH_FUNC_LEQUAL);
    // Without the private depth buffer the bomb is still drawn, only without self-occlusion between
    // its own surfaces; native scene-depth clipping is in the shader and is unaffected.
    if (!depthReady)
        Logging.Log("[remote-bomb] TARGET_DEPTH_FALLBACK drawing without private depth");
    Logging.Log("[remote-bomb] TARGET_READY images=%u bytes=%u cap=%u,%u depth=%u", depthReady ? 7u : 6u,
                unsigned(used), kWidth, kHeight, unsigned(depthReady));
    return true;
}
void begin(NVNcommandBuffer *command, unsigned width, unsigned height) {
    const NVNtexture *colors[]{&images[Surface], &images[Bright]};
    setTargets(command, 2, colors, nullptr, depthReady ? &images[Depth] : nullptr, nullptr);
    viewport(command, 0, 0, float(width), float(height)); scissor(command, 0, 0, width, height);
    bindDepth(command, depthReady ? &depthOn : &depthOff);
    const float zero[4]{};
    clearColor(command, 0, zero, 15); clearColor(command, 1, zero, 15);
    if (depthReady)
        clearDepth(command, 1.f, true, 0, 0);
}
void bind(NVNcommandBuffer *command, Image image, unsigned width, unsigned height) {
    const NVNtexture *color = &images[image];
    setTargets(command, 1, &color, nullptr, nullptr, nullptr);
    viewport(command, 0, 0, float(width), float(height)); scissor(command, 0, 0, width, height);
}
void meshDepth(NVNcommandBuffer *command, bool enabled) {
    bindDepth(command, enabled && depthReady ? &depthOn : &depthOff);
}
void effectsDepth(NVNcommandBuffer *command) { bindDepth(command, depthReady ? &depthRead : &depthOff); }
NVNtextureHandle handle(Image image, int sampler) { return textureHandle(device, ids[image], sampler); }
}
