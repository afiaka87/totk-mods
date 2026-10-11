// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombTargets.hpp"
#include "totk/render/GlowTargets.hpp"
#include <lib.hpp>
#include <cstring>
#include <type_traits>

namespace remote_bomb::targets {
namespace {
// Compressible pool for the DEPTH16 1600x900 depth (3,276,800 bytes); an uncompressed depth
// texture fails to initialize.
constexpr std::size_t kDepthBytes = 3328 * 1024;
alignas(65536) unsigned char depthMemory[kDepthBytes]{};
NVNmemoryPool depthPool{};
NVNtexture depthImage{};
bool depthReady{}, depthBound{};
std::uintptr_t moduleBase{};
const totk::render::GlowTargets *glow{};
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
    moduleBase = base;
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
    const auto manager = read<void *>(read<std::uintptr_t>(base + 0x462EF90));
    if (!manager) {
        Logging.Log("[remote-bomb] TARGET_MANAGER_MISSING");
        return false;
    }
    // Without the glow images the bomb is still drawn onto the game's frame, only without bloom.
    glow = totk::render::glowTargets(
        device, getProc, manager, reinterpret_cast<totk::render::RegisterTexture>(base + 0xA5B0B4),
        "remote-bomb");
    if (!glow)
        Logging.Log("[remote-bomb] TARGET_GLOW_FALLBACK drawing without bloom");
    NVNmemoryPoolBuilder depthBuilder{};
    poolDefaults(&depthBuilder); poolDevice(&depthBuilder, device);
    poolStorage(&depthBuilder, depthMemory, kDepthBytes);
    poolFlags(&depthBuilder, NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS | NVN_MEMORY_POOL_FLAGS_GPU_CACHED |
                                 NVN_MEMORY_POOL_FLAGS_COMPRESSIBLE);
    if (!poolInit(&depthPool, &depthBuilder)) {
        Logging.Log("[remote-bomb] TARGET_DEPTH_POOL_FAILED bytes=%u", unsigned(kDepthBytes));
    } else {
        NVNtextureBuilder texture{};
        defaults(&texture); setDevice(&texture, device); target(&texture, NVN_TEXTURE_TARGET_2D);
        size(&texture, kWidth, kHeight);
        format(&texture, NVN_FORMAT_DEPTH16);
        flags(&texture, NVN_TEXTURE_FLAGS_COMPRESSIBLE);
        const auto align = alignment(&texture), count = bytes(&texture);
        if (!align || (align & (align-1)) || align > kDepthBytes || !count || count > kDepthBytes)
            Logging.Log("[remote-bomb] TARGET_DEPTH_LAYOUT_FAILED alignment=%u bytes=%u", unsigned(align), unsigned(count));
        else {
            storage(&texture, &depthPool, 0);
            depthReady = init(&depthImage, &texture);
            if (!depthReady)
                Logging.Log("[remote-bomb] TARGET_DEPTH_TEXTURE_FAILED alignment=%u bytes=%u", unsigned(align), unsigned(count));
        }
    }
    stateDefaults(&depthOn); test(&depthOn, true); write(&depthOn, true); compare(&depthOn, NVN_DEPTH_FUNC_LESS);
    stateDefaults(&depthOff); test(&depthOff, false); write(&depthOff, false);
    stateDefaults(&depthRead); test(&depthRead, true); write(&depthRead, false); compare(&depthRead, NVN_DEPTH_FUNC_LEQUAL);
    // Without the private depth buffer the bomb is still drawn, only without self-occlusion between
    // its own surfaces; native scene-depth clipping is in the shader and is unaffected.
    if (!depthReady)
        Logging.Log("[remote-bomb] TARGET_DEPTH_FALLBACK drawing without private depth");
    Logging.Log("[remote-bomb] TARGET_READY glow_bytes=%u cap=%u,%u depth=DEPTH16 depth_pool=%u depth_ready=%u",
                glow ? unsigned(glow->bytes) : 0u, kWidth, kHeight, unsigned(kDepthBytes), unsigned(depthReady));
    return true;
}
bool begin(NVNcommandBuffer *command, void *context, unsigned width, unsigned height, bool &bloom) {
    totk::render::GameFrame frame{};
    if (!totk::render::gameFrame(moduleBase, context, frame))
        return false;
    const bool fits = width <= kWidth && height <= kHeight;
    bloom = glow && fits;
    depthBound = depthReady && fits;
    const NVNtexture *colors[]{frame.texture, glow ? &glow->images[totk::render::Bright] : nullptr};
    const NVNtextureView *views[]{frame.view, glow ? &glow->views[totk::render::Bright] : nullptr};
    setTargets(command, bloom ? 2 : 1, colors, views, depthBound ? &depthImage : nullptr, nullptr);
    viewport(command, 0, 0, float(width), float(height)); scissor(command, 0, 0, width, height);
    bindDepth(command, depthBound ? &depthOn : &depthOff);
    const float zero[4]{};
    if (bloom)
        clearColor(command, 1, zero, 15);
    if (depthBound)
        clearDepth(command, 1.f, true, 0, 0);
    return true;
}
void bind(NVNcommandBuffer *command, Image image, unsigned width, unsigned height) {
    const NVNtexture *color = &glow->images[image];
    setTargets(command, 1, &color, nullptr, nullptr, nullptr);
    viewport(command, 0, 0, float(width), float(height)); scissor(command, 0, 0, width, height);
}
void meshDepth(NVNcommandBuffer *command, bool enabled) {
    bindDepth(command, enabled && depthBound ? &depthOn : &depthOff);
}
void effectsDepth(NVNcommandBuffer *command) { bindDepth(command, depthBound ? &depthRead : &depthOff); }
NVNtextureHandle handle(Image image, int sampler) { return textureHandle(device, glow->ids[image], sampler); }
}
