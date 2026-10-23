// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombRender.hpp"
#include "totk/engine/CodeCheck.hpp"
#include "BombRuntime.hpp"
#include "BombRenderPolicy.hpp"
#include "BombHighlight.hpp"
#include "BombVisualBackend.hpp"
#include "BombTargets.hpp"
#include "RemoteBombShaders.hpp"
#include "RemoteBombAssets.hpp"
#include "RemoteBombHudAssets.hpp"
#include "RemoteBombEffectAssets.hpp"
#include "totk/render/PfxHook.hpp"
#include "totk/render/AbilityHudLayout.hpp"
#ifdef BOTW_ABILITIES_CHAMPIONS
#include "totk/render/HudExtension.hpp"
#endif
#include "totk/engine/Pointer.hpp"
#include <nvn/nvn.h>
#include <lib.hpp>
#include <cstring>
#include <type_traits>

namespace remote_bomb::render {
namespace {
using render_policy::Camera;
using render_policy::Uniforms;
std::uintptr_t base{};
NVNdevice *device{};
PFNNVNDEVICEGETPROCADDRESSPROC getProc{};
NVNmemoryPool codePool{}, meshPool{}, texturePool{};
NVNprogram program{}, hudProgram{}, effectsProgram{}, blurProgram{}, downsampleProgram{}, compositeProgram{};
struct Shader { const unsigned char *code; unsigned size; const unsigned char *control; };
#define BOMB_SHADER(name) {shaders::name##Code, sizeof(shaders::name##Code), shaders::name##Control}
#if defined(BOTW_ABILITIES_NATIVE_SPHERE) && defined(BOTW_ABILITIES_NATIVE_CUBE)
constexpr bool kHudOnly = true;
constexpr Shader shaderStages[]{BOMB_SHADER(hudvert), BOMB_SHADER(hudfrag)};
#else
constexpr bool kHudOnly = false;
constexpr Shader shaderStages[]{BOMB_SHADER(bombvert), BOMB_SHADER(bombfrag),
    BOMB_SHADER(hudvert), BOMB_SHADER(hudfrag), BOMB_SHADER(effectsvert), BOMB_SHADER(effectsfrag),
    BOMB_SHADER(postvert), BOMB_SHADER(blur), BOMB_SHADER(downsample), BOMB_SHADER(composite)};
#endif
#undef BOMB_SHADER
constexpr unsigned kBaseTextureCount = kHudOnly ? 7 : 36;
#ifdef BOTW_ABILITIES_CHAMPIONS
constexpr unsigned kTextureCount = kBaseTextureCount + totk::render::kExtraHudIcons;
bool extraHudVisible() {
    const auto* extension = totk::render::hudExtension();
    totk::render::HudFrame frame;
    return extension && extension->read(frame) && frame.count;
}
#else
constexpr unsigned kTextureCount = kBaseTextureCount;
bool extraHudVisible() { return false; }
#endif
constexpr unsigned kHudTextureStart = kHudOnly ? 0 : 8;
constexpr unsigned codePoolBytes() {
    if constexpr (!kHudOnly) return 65536;
    unsigned end = 0;
    for (const auto &shader : shaderStages) end = ((end + 255) & ~255u) + shader.size;
    return (end + 16384 + 4095) & ~4095u;
}
// Bomb glow strength relative to the original material emission (surface glow and bloom both scale).
constexpr float kGlowScale = .15f;
// Glow colour: a lighter, less saturated blue than the material's deep (0.025, 0.25, 1.0) at the same luminance, so kGlowScale alone sets the brightness.
constexpr float kGlowColor[3]{.136f, .272f, .453f};
// Ultrahand outline width as a share of the screen height (about 4 pixels at 1080p).
constexpr float kOutlineHeight = .004f;
float centres[2][3]{};
NVNtexture textures[kTextureCount]{};
int textureIds[kTextureCount]{}, samplerId = -1;
alignas(4096) unsigned char codeMemory[codePoolBytes()]{};
alignas(4096) unsigned char meshMemory[131072]{};
// HUD-only storage retains alignment headroom; every native allocation is bounds-checked.
alignas(65536) unsigned char textureMemory[(kHudOnly ? 12 : 22) * 65536]{};
NVNbufferAddress meshAddress{}, indexAddress[2]{};
bool initialized{}, attempted{};
unsigned refusals{};
PFNNVNCOMMANDBUFFERBINDPROGRAMPROC bindProgram{};
PFNNVNCOMMANDBUFFERBINDUNIFORMBUFFERPROC bindUniform{};
PFNNVNCOMMANDBUFFERDRAWELEMENTSPROC drawElements{};
PFNNVNCOMMANDBUFFERDRAWARRAYSPROC drawArrays{};
PFNNVNCOMMANDBUFFERBINDTEXTUREPROC bindTexture{};
PFNNVNDEVICEGETTEXTUREHANDLEPROC textureHandle{};
PFNNVNCOMMANDBUFFERSETSCISSORPROC scissor{};
PFNNVNCOMMANDBUFFERSETVIEWPORTPROC viewport{};
PFNNVNCOMMANDBUFFERBARRIERPROC barrier{};
alignas(8) unsigned char samplerBindings[128]{};
unsigned samplerLocations[4]{0xffff00ff, 0, 0, 0};
template <class T> T read(const void *pointer, std::size_t offset) {
    T value{};
    if (pointer)
        std::memcpy(&value, static_cast<const unsigned char *>(pointer) + offset, sizeof(value));
    return value;
}
template <class T> void write(void *pointer, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char *>(pointer) + offset, &value, sizeof(value));
}
template <class F> F native(std::uintptr_t offset) { return reinterpret_cast<F>(base + offset); }
void *global(std::uintptr_t offset) { return read<void *>(reinterpret_cast<void *>(base), offset); }
bool refuse(unsigned reason) {
    if (reason < 32 && !(refusals & (1u << reason))) {
        refusals |= 1u << reason;
        Logging.Log("[remote-bomb] RENDER_REFUSED reason=%u", reason);
    }
    return false;
}
template <class F> bool resolve(F &function, const char *name) {
    function = reinterpret_cast<F>(getProc(device, name));
    if (!function)
        Logging.Log("[remote-bomb] NVN_MISSING name=%s", name);
    return function != nullptr;
}
bool initializePools() {
    auto graphics = global(0x4716E48);
    getProc = reinterpret_cast<PFNNVNDEVICEGETPROCADDRESSPROC>(global(0x46707E8));
    if (!graphics || !getProc)
        return refuse(1);
    device = read<NVNdevice *>(graphics, 0x30);
    if (!device)
        return refuse(2);
    attempted = true;
    PFNNVNMEMORYPOOLBUILDERSETDEFAULTSPROC defaults{};
    PFNNVNMEMORYPOOLBUILDERSETDEVICEPROC setDevice{};
    PFNNVNMEMORYPOOLBUILDERSETSTORAGEPROC storage{};
    PFNNVNMEMORYPOOLBUILDERSETFLAGSPROC flags{};
    PFNNVNMEMORYPOOLINITIALIZEPROC init{};
    PFNNVNMEMORYPOOLFLUSHMAPPEDRANGEPROC flush{};
    PFNNVNMEMORYPOOLGETBUFFERADDRESSPROC address{};
    PFNNVNPROGRAMINITIALIZEPROC programInit{};
    PFNNVNPROGRAMSETSHADERSPROC shadersSet{};
    PFNNVNDEVICEGETINTEGERPROC integer{};
    if (!resolve(defaults, "nvnMemoryPoolBuilderSetDefaults") ||
        !resolve(setDevice, "nvnMemoryPoolBuilderSetDevice") ||
        !resolve(storage, "nvnMemoryPoolBuilderSetStorage") ||
        !resolve(flags, "nvnMemoryPoolBuilderSetFlags") || !resolve(init, "nvnMemoryPoolInitialize") ||
        !resolve(flush, "nvnMemoryPoolFlushMappedRange") || !resolve(address, "nvnMemoryPoolGetBufferAddress") ||
        !resolve(programInit, "nvnProgramInitialize") || !resolve(shadersSet, "nvnProgramSetShaders") ||
        !resolve(integer, "nvnDeviceGetInteger") || !resolve(bindProgram, "nvnCommandBufferBindProgram") ||
        !resolve(bindUniform, "nvnCommandBufferBindUniformBuffer") || !resolve(drawElements, "nvnCommandBufferDrawElements") ||
        !resolve(drawArrays, "nvnCommandBufferDrawArrays") ||
        !resolve(bindTexture, "nvnCommandBufferBindTexture") || !resolve(textureHandle, "nvnDeviceGetTextureHandle") ||
        !resolve(scissor, "nvnCommandBufferSetScissor") || !resolve(viewport, "nvnCommandBufferSetViewport") ||
        !resolve(barrier, "nvnCommandBufferBarrier"))
        return refuse(3);
    NVNmemoryPoolBuilder builder{};
    const auto pool = [&](NVNmemoryPool &out, void *bytes, std::size_t size, int extra) {
        defaults(&builder); setDevice(&builder, device); storage(&builder, bytes, size);
        flags(&builder, NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED | extra);
        return init(&out, &builder);
    };
    unsigned offsets[std::size(shaderStages)]{}, codeBytes{};
    for (unsigned i = 0; i < std::size(shaderStages); ++i) {
        offsets[i] = (codeBytes+255) & ~255u;
        codeBytes = offsets[i] + shaderStages[i].size;
    }
    int padding = -1;
    integer(device, NVN_DEVICE_INFO_SHADER_CODE_MEMORY_POOL_PADDING_SIZE, &padding);
    if (padding < 0 || codeBytes + unsigned(padding) > sizeof(codeMemory))
        return refuse(4);
    for (unsigned i = 0; i < std::size(shaderStages); ++i)
        std::memcpy(codeMemory + offsets[i], shaderStages[i].code, shaderStages[i].size);
    if (!pool(codePool, codeMemory, sizeof(codeMemory), NVN_MEMORY_POOL_FLAGS_SHADER_CODE) ||
        (!kHudOnly && !pool(meshPool, meshMemory, sizeof(meshMemory), 0)) ||
        !pool(texturePool, textureMemory, sizeof(textureMemory), 0))
        return refuse(5);
    const auto codeAddress = address(&codePool);
    if constexpr (!kHudOnly) meshAddress = address(&meshPool);
    if (!codeAddress || (!kHudOnly && !meshAddress))
        return refuse(6);
    if constexpr (!kHudOnly) for (unsigned form = 0; form < 2; ++form) {
        constexpr unsigned rowBytes = 3420 * 16, indexOffset = (rowBytes + 255) & ~255u;
        const auto &mesh = assets::meshes[form];
        if (indexOffset + mesh.indexCount * sizeof(std::uint16_t) > 65536)
            return refuse(7);
        std::memcpy(meshMemory + form * 65536, mesh.rows, rowBytes);
        std::memcpy(meshMemory + form * 65536 + indexOffset, mesh.indices, mesh.indexCount * sizeof(std::uint16_t));
        indexAddress[form] = meshAddress + form * 65536 + indexOffset;
        const auto centre = render_policy::centre(mesh.rows, mesh.indices, mesh.indexCount);
        std::memcpy(centres[form], centre.data(), sizeof(centres[form]));
    }
    flush(&codePool, 0, sizeof(codeMemory));
    if constexpr (!kHudOnly) flush(&meshPool, 0, sizeof(meshMemory));
#if defined(BOTW_ABILITIES_NATIVE_SPHERE) && defined(BOTW_ABILITIES_NATIVE_CUBE)
    NVNprogram *programs[]{&hudProgram};
    constexpr unsigned indices[][2]{{0,1}};
#else
    NVNprogram *programs[]{&program, &hudProgram, &effectsProgram, &blurProgram, &downsampleProgram, &compositeProgram};
    constexpr unsigned indices[][2]{{0,1}, {2,3}, {4,5}, {6,7}, {6,8}, {6,9}};
#endif
    for (unsigned i = 0; i < std::size(programs); ++i) {
        if (!programInit(programs[i], device)) return refuse(8);
        const auto v = indices[i][0], f = indices[i][1];
        const NVNshaderData stages[]{{codeAddress+offsets[v], shaderStages[v].control},
                                     {codeAddress+offsets[f], shaderStages[f].control}};
        if (!shadersSet(programs[i], 2, stages)) return refuse(9);
    }
    return true;
}
bool initializeTextures() {
    PFNNVNTEXTUREBUILDERSETDEFAULTSPROC defaults{};
    PFNNVNTEXTUREBUILDERSETDEVICEPROC setDevice{};
    PFNNVNTEXTUREBUILDERSETTARGETPROC target{};
    PFNNVNTEXTUREBUILDERSETSIZE2DPROC size{};
    PFNNVNTEXTUREBUILDERSETFORMATPROC format{};
    PFNNVNTEXTUREBUILDERSETSWIZZLEPROC swizzle{};
    PFNNVNTEXTUREBUILDERGETSTORAGESIZEPROC bytes{};
    PFNNVNTEXTUREBUILDERGETSTORAGEALIGNMENTPROC alignment{};
    PFNNVNTEXTUREBUILDERSETSTORAGEPROC storage{};
    PFNNVNTEXTUREINITIALIZEPROC init{};
    PFNNVNTEXTUREWRITETEXELSPROC writePixels{};
    PFNNVNTEXTUREFLUSHTEXELSPROC flush{};
    if (!resolve(defaults, "nvnTextureBuilderSetDefaults") || !resolve(setDevice, "nvnTextureBuilderSetDevice") ||
        !resolve(target, "nvnTextureBuilderSetTarget") || !resolve(size, "nvnTextureBuilderSetSize2D") ||
        !resolve(format, "nvnTextureBuilderSetFormat") || !resolve(swizzle, "nvnTextureBuilderSetSwizzle") ||
        !resolve(bytes, "nvnTextureBuilderGetStorageSize") ||
        !resolve(alignment, "nvnTextureBuilderGetStorageAlignment") || !resolve(storage, "nvnTextureBuilderSetStorage") ||
        !resolve(init, "nvnTextureInitialize") || !resolve(writePixels, "nvnTextureWriteTexels") ||
        !resolve(flush, "nvnTextureFlushTexels"))
        return refuse(3);
    auto manager = read<void *>(global(0x462EF90), 0);
    if (!manager)
        return refuse(10);
    std::size_t used = 0;
    const auto upload = [&](unsigned i, const auto& source) {
        NVNtextureBuilder builder{};
        defaults(&builder); setDevice(&builder, device); target(&builder, NVN_TEXTURE_TARGET_2D);
        size(&builder, source.width, source.height); format(&builder, NVNformat(source.format));
        swizzle(&builder, NVNtextureSwizzle(source.swizzle[0]), NVNtextureSwizzle(source.swizzle[1]),
                NVNtextureSwizzle(source.swizzle[2]), NVNtextureSwizzle(source.swizzle[3]));
        const auto align = alignment(&builder), count = bytes(&builder);
        if (!align || (align & (align - 1)) || !count)
            return refuse(11);
        used = (used + align - 1) & ~(align - 1);
        if (used + count > sizeof(textureMemory)) {
            Logging.Log("[remote-bomb] TEXTURE_POOL_FULL index=%u need=%u pool=%u", i, unsigned(used + count),
                        unsigned(sizeof(textureMemory)));
            return refuse(12);
        }
        storage(&builder, &texturePool, used);
        used += count;
        if (!init(&textures[i], &builder))
            return refuse(13);
        NVNcopyRegion region{0, 0, 0, int(source.width), int(source.height), 1};
#if defined(BOTW_ABILITIES_STREAM_TEXTURES)
        if constexpr (std::is_pointer_v<decltype(source.data)>) {
            writePixels(&textures[i], nullptr, &region, source.data);
            flush(&textures[i], nullptr, &region);
        } else {
        if (!totk::assets::withTexture(totk::assets::kBombTexturePath, source.data,
                [&](const unsigned char *pixels) {
                    writePixels(&textures[i], nullptr, &region, pixels);
                    flush(&textures[i], nullptr, &region);
                })) return refuse(15);
        }
#else
        writePixels(&textures[i], nullptr, &region, source.data);
        flush(&textures[i], nullptr, &region);
#endif
        textureIds[i] = native<int (*)(void *, NVNtexture *, NVNtextureView *)>(0xA5B0B4)(manager, &textures[i], nullptr);
        if (textureIds[i] < 0)
            return refuse(14);
        return true;
    };
    for (unsigned i = 0; i < kBaseTextureCount; ++i) {
        const auto& source = kHudOnly ? hud_assets::textures[i] : i < 8 ? assets::textures[i] :
                             i < 15 ? hud_assets::textures[i - 8] : effect_assets::textures[i - 15];
        if (!upload(i,source)) return false;
    }
#ifdef BOTW_ABILITIES_CHAMPIONS
    const auto* extension = totk::render::hudExtension();
    if (!extension) return refuse(30);
    for (unsigned i = 0; i < totk::render::kExtraHudIcons; ++i)
        if (!upload(kBaseTextureCount+i,extension->textures[i])) return false;
#endif
    write<unsigned>(samplerBindings, 0x68, 1);
    write<const void *>(samplerBindings, 0x70, samplerLocations);
    Logging.Log("[remote-bomb] GPU_READY hud_only=%u textures=%u texture_bytes=%u pool=%u", unsigned(kHudOnly), kTextureCount, unsigned(used),
                unsigned(sizeof(textureMemory)));
    return true;
}
bool initialize() {
    if (initialized)
        return true;
    if (attempted)
        return false;
    initialized = initializePools() && initializeTextures() && (kHudOnly || targets::initialize(device, getProc, base));
    return initialized;
}
struct Resources { NVNcommandBuffer *command{}; void *sampler{}; unsigned width{}, height{}; float depth[2]{}, sampleScale[2]{1,1}; };
bool resources(void *drawContext, void *scene, void *context, Resources &out) {
    if (!drawContext || !scene || !context || !read<void *>(context, 0x540) || !read<void *>(context, 0x548))
        return refuse(15);
    auto command = read<NVNcommandBuffer *>(drawContext, 0xB8);
    auto buffers = read<const void *>(scene, 0x1EC0);
    if (!command || !buffers || read<unsigned>(buffers, 8) < 1)
        return refuse(16);
    auto record = read<unsigned char *>(buffers, 0x10);
    const auto flags = read<unsigned>(record, 8);
    const unsigned slot = flags & 1 ? 0x700 : flags & 2 ? 0xE40 : 0;
    if (!record || !slot)
        return refuse(17);
    const auto sampler = record + slot;
    const auto width = read<std::uint16_t>(sampler, 0x30), height = read<std::uint16_t>(sampler, 0x32);
    if (width < 16 || height < 16 || width > 8192 || height > 8192) {
        if (!(refusals & (1u << 18)))
            Logging.Log("[remote-bomb] TARGET_VIEWPORT_REFUSED width=%u height=%u", unsigned(width), unsigned(height));
        return refuse(18);
    }
    static bool large{};
    if (!large && (width > targets::kWidth || height > targets::kHeight)) {
        Logging.Log("[remote-bomb] TARGET_VIEWPORT_LARGE width=%u height=%u cap=%u,%u drawing without bloom",
                    unsigned(width), unsigned(height), targets::kWidth, targets::kHeight);
        large = true;
    }
    out = {command, sampler, width, height};
    return true;
}
bool ensureSampler(const Resources &r) {
    if (samplerId < 0) {
        alignas(8) unsigned char descriptor[40];
        std::memcpy(descriptor, static_cast<unsigned char *>(r.sampler) + 184, sizeof(descriptor));
        write<float>(descriptor, 16, 0); write<float>(descriptor, 20, 0); write<float>(descriptor, 24, 0);
        descriptor[28] = NVN_MAG_FILTER_LINEAR; descriptor[29] = NVN_MIN_FILTER_LINEAR; descriptor[30] = 0;
        descriptor[31] = descriptor[32] = descriptor[33] = NVN_WRAP_MODE_REPEAT;
        descriptor[34] = 1; descriptor[36] = NVN_SAMPLER_REDUCTION_AVERAGE;
        write<std::uint16_t>(descriptor, 38, 0);
        auto manager = read<void *>(global(0x462EF90), 0);
        if (!manager)
            return refuse(20);
        const auto id = native<unsigned (*)(void *, const void *, const char *)>(0x7F91E0)(manager, descriptor, "Remote Bomb original maps");
        if (id >= 0xffff)
            return refuse(20);
        samplerId = int(id);
    }
    return true;
}
bool bindTextures(void *drawContext, const Resources &r, unsigned form) {
    if (!ensureSampler(r) ||
        !native<bool (*)(void *, void *, unsigned, void *)>(0x95EF6C)(samplerBindings, drawContext, 0, r.sampler))
        return refuse(19);
    barrier(r.command, NVN_BARRIER_INVALIDATE_TEXTURE_DESCRIPTOR_BIT);
    for (unsigned map = 0; map < 4; ++map)
        bindTexture(r.command, NVN_SHADER_STAGE_FRAGMENT, map + 1,
                    textureHandle(device, textureIds[form * 4 + map], samplerId));
    return true;
}
float curve(const effect_assets::Curve &source, float time, unsigned channel, float initial) {
    float value = initial, previousTime = 0.f;
    for (unsigned key = 0; key < source.count; ++key) {
        const auto nextTime = source.keys[key][3], nextValue = source.keys[key][channel];
        if (time <= nextTime && nextTime > previousTime)
            return value + (nextValue-value) * std::clamp((time-previousTime)/(nextTime-previousTime), 0.f, 1.f);
        value = nextValue;
        previousTime = nextTime;
    }
    return value;
}
// Copies uniforms into a block from the game's per-frame allocator; returns its GPU address or 0.
template <class T> std::uint64_t uniformBlock(const T &uniform) {
    void *block{};
    native<void (*)(void **, const void *, std::size_t)>(0x95F604)(&block, &uniform, sizeof(uniform));
    return read<std::uint64_t>(block, 0x78);
}
void bindUniforms(NVNcommandBuffer *command, std::uint64_t address, std::size_t size) {
    for (const auto stage : {NVN_SHADER_STAGE_VERTEX, NVN_SHADER_STAGE_FRAGMENT})
        bindUniform(command, stage, 0, address, size);
}
bool allocatorReady() {
    const auto allocator = global(0x4717048);
    return allocator && read<void *>(allocator, 24) && read<unsigned>(allocator, 72);
}
// The game's 128-byte graphics state: resetState loads its defaults, applyState sends it to the GPU.
void resetState(void *state) { native<void (*)(void *)>(0x74C19C)(state); }
void applyState(void *state, void *drawContext) { native<void (*)(void *, void *)>(0x756A08)(state, drawContext); }
// Blends the render targets in mask (the word at +4) from the given colour factors, keeping the destination alpha. Each target's factors and equations are six bytes from 40 + 6 * target.
void blend(unsigned char *state, unsigned mask, NVNblendFunc source, NVNblendFunc destination) {
    write<unsigned>(state, 4, mask);
    for (unsigned target = 0; target < 2; ++target) {
        if (!(mask & (1u << target)))
            continue;
        auto *factors = state + 40 + 6 * target;
        factors[0] = source;
        factors[1] = NVN_BLEND_FUNC_ZERO;
        factors[2] = destination;
        factors[3] = NVN_BLEND_FUNC_ONE;
        factors[4] = factors[5] = NVN_BLEND_EQUATION_ADD;
    }
}
void drawEffectLayer(const Resources &r, const Camera &camera, effects::Position position,
                     unsigned emitter, float radius, float progress, float kind, float opacity,
                     unsigned count = 1) {
    const auto &source = effect_assets::emitters[emitter];
    const std::array<float, 9> identity{1,0,0,0,1,0,0,0,1};
    auto uniform = render_policy::uniforms(camera, position, identity, r.width, r.height);
    uniform.lighting[0] = radius * curve(source.scale, progress, 0, 1.f);
    uniform.lighting[1] = progress;
    uniform.lighting[2] = kind;
    uniform.lighting[3] = opacity * curve(source.curves[1], progress, 0, source.colors[0][3]);
    for (unsigned channel = 0; channel < 3; ++channel)
        uniform.emission[channel] = curve(source.curves[0], progress, channel, source.colors[0][channel]);
    uniform.depth[0] = r.depth[0];
    uniform.depth[1] = r.depth[1];
    uniform.depth[2] = r.sampleScale[0]; uniform.depth[3] = r.sampleScale[1];
    const auto block = uniformBlock(uniform);
    if (!block) { refuse(23); return; }
    for (unsigned map = 0; map < 3; ++map) {
        const auto texture = source.textures[map] >= 0 ? source.textures[map] : int(effect_assets::ukglow05);
        bindTexture(r.command, NVN_SHADER_STAGE_FRAGMENT, map+1,
                    textureHandle(device, textureIds[15+unsigned(texture)], samplerId));
    }
    bindUniforms(r.command, block, sizeof(Uniforms));
    drawArrays(r.command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 6*count);
}
void drawEffects(const Resources &r, const Camera &camera, const effects::Timeline &timeline) {
    using namespace effect_assets;
    bindProgram(r.command, &effectsProgram, 63);
    for (unsigned form = 0; form < 2; ++form) {
        const auto &burst = timeline.bursts[form];
        if (burst.active) {
            const auto age = burst.frames;
            if (age >= 2.f) {
                const auto progress = (age-2.f)/(effects::kBurstFrames-2.f);
                const auto size = .3f+.25f*progress;
                drawEffectLayer(r, camera, burst.position, BombAppear_Glow, size*2.f, progress, 6, .18f);
                drawEffectLayer(r, camera, burst.position, BombAppear_Ring, size, progress, 1, .25f*(1.f-progress));
                drawEffectLayer(r, camera, burst.position, BombAppear_Flear0, size*3.f, progress, 5, .2f);
                drawEffectLayer(r, camera, burst.position, BombAppear_Ptcl_main, size, progress, 3, .3f, 12);
            }
        }
    }
}
struct FilterUniforms { float sizes[4]{}, kernel[4]{}; };
struct FilterBlocks { std::uint64_t values[9]{}; };
// Pass order: Bright to NarrowA, NarrowA to WideA, then the narrow and wide blurs; the last block is the bloom composite. Sizes are source then destination; the kernel is the blur direction and the downsample factor.
bool allocateFilters(const Resources &r, FilterBlocks &blocks) {
    const unsigned glowW=totk::render::kGlowWidth, glowH=totk::render::kGlowHeight;
    const float fullW=float(glowW), fullH=float(glowH);
    const float halfW=float((glowW+1)/2), halfH=float((glowH+1)/2);
    const float wideW=float((glowW+3)/4), wideH=float((glowH+3)/4);
    const float kernelX=fullW/r.width, kernelY=fullH/r.height;
    const FilterUniforms uniforms[9]{
        {{fullW, fullH, halfW, halfH}, {0, 0, 2, 0}}, {{halfW, halfH, wideW, wideH}, {0, 0, 2, 0}},
        {{halfW, halfH, halfW, halfH}, {kernelX, 0, 0, 0}}, {{halfW, halfH, halfW, halfH}, {0, kernelY, 0, 0}},
        {{wideW, wideH, wideW, wideH}, {kernelX, 0, 0, 0}}, {{wideW, wideH, wideW, wideH}, {0, kernelY, 0, 0}},
        {{wideW, wideH, wideW, wideH}, {kernelX, 0, 0, 0}}, {{wideW, wideH, wideW, wideH}, {0, kernelY, 0, 0}},
        {{fullW, fullH, fullW, fullH}, {0, 0, 0, 0}}};
    for (unsigned i = 0; i < 9; ++i)
        if (!(blocks.values[i] = uniformBlock(uniforms[i])))
            return refuse(29);
    return true;
}
void postprocess(const Resources &r, void *context, void *drawContext, const FilterBlocks &blocks) {
    const auto bindBlock = [&](unsigned i) { bindUniforms(r.command, blocks.values[i], sizeof(FilterUniforms)); };
    const auto bindImage = [&](unsigned location, targets::Image image) {
        bindTexture(r.command, NVN_SHADER_STAGE_FRAGMENT, location, targets::handle(image, samplerId));
    };
    const auto textureBarrier = [&]() {
        barrier(r.command, NVN_BARRIER_ORDER_FRAGMENTS_BIT | NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
    };
    using namespace totk::render;
    // The wide chain starts from NarrowA, so NarrowB and the wide images can reuse Bright's memory.
    constexpr targets::Image sources[]{Bright, NarrowA, NarrowA, NarrowB, WideA, WideB, WideA, WideB};
    constexpr targets::Image destinations[]{NarrowA, WideA, NarrowB, NarrowA, WideB, WideA, WideB, WideA};
    alignas(8) unsigned char state[128]{};
    resetState(state);
    state[0] = state[1] = 0;
    write<unsigned>(state, 4, 0);
    applyState(state, drawContext);
    targets::meshDepth(r.command, false);
    barrier(r.command, NVN_BARRIER_ORDER_PRIMITIVES_BIT | NVN_BARRIER_ORDER_FRAGMENTS_BIT |
                       NVN_BARRIER_INVALIDATE_TEXTURE_BIT | NVN_BARRIER_INVALIDATE_ZCULL_BIT);
    for (unsigned i = 0; i < 8; ++i) {
        textureBarrier(); bindImage(0, sources[i]);
        const unsigned divisor = i == 0 || i == 2 || i == 3 ? 2 : 4;
        targets::bind(r.command, destinations[i], (kGlowWidth+divisor-1)/divisor, (kGlowHeight+divisor-1)/divisor);
        bindProgram(r.command, i < 2 ? &downsampleProgram : &blurProgram, 63);
        bindBlock(i);
        drawArrays(r.command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    }
    textureBarrier();
    native<void (*)(void *, void *)>(0xC5A6FC)(context, drawContext);
    viewport(r.command, 0, 0, float(r.width), float(r.height)); scissor(r.command, 0, 0, r.width, r.height);
    blend(state, 1, NVN_BLEND_FUNC_ONE, NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA);
    applyState(state, drawContext);
    bindImage(1, NarrowA); bindImage(2, WideA);
    bindProgram(r.command, &compositeProgram, 63); bindBlock(8);
    drawArrays(r.command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
}
void draw(void *drawContext, void *scene, void *context, const Camera &camera) {
    runtime::Presentation frame{};
    if (!runtime::presentation(frame) && !extraHudVisible())
        return;
    for (unsigned form = 0; form < 2; ++form) {
        if (nativeBomb(form ? Shape::Cube : Shape::Sphere)) {
            frame.bombs[form] = {};
            frame.effects.bursts[form] = {};
        }
    }
    if (!frame.bombs[0].live && !frame.bombs[1].live && !frame.remembered && !effects::visible(frame.effects) && !extraHudVisible())
        return;
    Resources r;
    if (!resources(drawContext, scene, context, r) || !initialize())
        return;
    if (kHudOnly || (!frame.bombs[0].live && !frame.bombs[1].live && !effects::visible(frame.effects))) {
        ensureSampler(r);
        return;
    }
    if (!allocatorReady()) {
        refuse(21);
        return;
    }
    std::array<float, 3> live[2]{};
    unsigned liveCount = 0;
    for (const auto &bomb : frame.bombs)
        if (bomb.live)
            live[liveCount++] = {bomb.position.x, bomb.position.y, bomb.position.z};
    const auto span = render_policy::depthRange(camera, live, liveCount);
    r.depth[0] = span[0];
    r.depth[1] = span[1];
    // [form][part][pass]: pass 0 is the body, pass 1 the Ultrahand outline shell (highlighted bombs only).
    std::uint64_t blocks[2][2][2]{}, glowBlocks[2][2][2]{};
    unsigned passes[2]{};
    for (unsigned form = 0; form < 2; ++form) {
        if (!frame.bombs[form].live)
            continue;
        const auto &bomb = frame.bombs[form];
        const auto &mesh = assets::meshes[form];
        if (mesh.drawCount != 2) { refuse(22); return; }
        passes[form] = bomb.highlight == highlight::Level::None ? 1 : 2;
        for (unsigned part = 0; part < mesh.drawCount; ++part) {
            auto uniform = render_policy::uniforms(camera, {bomb.position.x, bomb.position.y, bomb.position.z},
                                                   bomb.rotation, r.width, r.height);
            uniform.lighting[3] = mesh.draws[part].mask ? 1.f : 0.f;
            std::memcpy(uniform.emission, mesh.draws[part].emission, sizeof(uniform.emission));
            if (uniform.emission[3] > 0.f)
                std::memcpy(uniform.emission, kGlowColor, sizeof(kGlowColor));
            uniform.emission[3] *= kGlowScale;
            const auto tint = highlight::colour(bomb.highlight);
            std::memcpy(uniform.highlight, tint.data(), sizeof(float) * 3);
            std::memcpy(uniform.outline, centres[form], sizeof(centres[form]));
            uniform.outline[3] = kOutlineHeight;
            uniform.depth[0] = r.depth[0];
            uniform.depth[1] = r.depth[1];
            for (unsigned pass = 0; pass < passes[form]; ++pass) {
                uniform.highlight[3] = passes[form] == 1 ? 0.f : float(pass + 1);
                uniform.depth[2] = uniform.depth[3] = 1.f;
                if (!(blocks[form][part][pass] = uniformBlock(uniform))) { refuse(23); return; }
                uniform.depth[2] = float(r.width) / totk::render::kGlowWidth;
                uniform.depth[3] = float(r.height) / totk::render::kGlowHeight;
                if (!(glowBlocks[form][part][pass] = uniformBlock(uniform))) { refuse(23); return; }
            }
        }
    }
    if (!bindTextures(drawContext, r, 0))
        return;
    FilterBlocks filters;
    if (!allocateFilters(r, filters)) return;
    alignas(8) unsigned char graphics[128]{};
    resetState(graphics);
    graphics[0] = graphics[1] = 0;
    graphics[101] = NVN_FACE_NONE;
    write<unsigned>(graphics, 4, 0);
    applyState(graphics, drawContext);
    // Another glow pass in the same module may still be sampling the shared targets.
    barrier(r.command, NVN_BARRIER_ORDER_FRAGMENTS_BIT | NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
    // The game's own bind first, then the bomb draws straight onto its frame. The body is opaque: it replaces the frame's colour and keeps the frame's alpha.
    native<void (*)(void *, void *)>(0xC5A6FC)(context, drawContext);
    bool bloom{};
    if (!targets::begin(r.command, context, r.width, r.height, bloom)) {
        refuse(30);
        native<void (*)(void *, void *)>(0x962C18)(context, drawContext);
        native<void (*)(void *, void *)>(0xC4B7AC)(read<void *>(context, 0x540), drawContext);
        resetState(graphics);
        applyState(graphics, drawContext);
        return;
    }
    blend(graphics, 1, NVN_BLEND_FUNC_ONE, NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA);
    applyState(graphics, drawContext);
    const auto drawObjects = [&](const std::uint64_t (&drawBlocks)[2][2][2], const Resources &drawResources) {
        targets::meshDepth(r.command, true);
        bindProgram(r.command, &program, 63);
        for (unsigned form = 0; form < 2; ++form) {
            if (!frame.bombs[form].live || !bindTextures(drawContext, r, form))
                continue;
            bindUniform(r.command, NVN_SHADER_STAGE_VERTEX, 1, meshAddress + form * 65536, 3420 * 16);
            const auto &mesh = assets::meshes[form];
            // The outline shell goes first so the body drawn over it covers everything inside the edge.
            for (unsigned pass = passes[form]; pass-- > 0;)
                for (unsigned part = 0; part < mesh.drawCount; ++part) {
                    bindUniforms(r.command, drawBlocks[form][part][pass], sizeof(Uniforms));
                    drawElements(r.command, NVN_DRAW_PRIMITIVE_TRIANGLES, NVN_INDEX_TYPE_UNSIGNED_SHORT,
                                  mesh.draws[part].count, indexAddress[form] + mesh.draws[part].first * sizeof(std::uint16_t));
                }
        }
        if (effects::visible(frame.effects)) {
            blend(graphics, 3, NVN_BLEND_FUNC_ONE, NVN_BLEND_FUNC_ONE);
            applyState(graphics, drawContext);
            targets::effectsDepth(r.command);
            drawEffects(drawResources, camera, frame.effects);
        }
    };
    drawObjects(blocks, r);
    if (bloom) {
        barrier(r.command, NVN_BARRIER_ORDER_PRIMITIVES_BIT | NVN_BARRIER_ORDER_FRAGMENTS_BIT |
                           NVN_BARRIER_INVALIDATE_TEXTURE_BIT | NVN_BARRIER_INVALIDATE_ZCULL_BIT);
        targets::emission(r.command);
        blend(graphics, 2, NVN_BLEND_FUNC_ONE, NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA);
        applyState(graphics, drawContext);
        auto glowResources = r;
        glowResources.sampleScale[0] = float(r.width) / totk::render::kGlowWidth;
        glowResources.sampleScale[1] = float(r.height) / totk::render::kGlowHeight;
        drawObjects(glowBlocks, glowResources);
    }
    if (bloom)
        postprocess(r, context, drawContext, filters);
    native<void (*)(void *, void *)>(0x962C18)(context, drawContext);
    native<void (*)(void *, void *)>(0xC4B7AC)(read<void *>(context, 0x540), drawContext);
    resetState(graphics);
    applyState(graphics, drawContext);
}
totk::render::PfxCallback previous{};
std::uint64_t pfx(void *extension, void *args) {
    const auto result = previous(extension, args);
    if (!args || read<unsigned>(args, 0x18) != 1 || read<unsigned>(args, 0x1C) != 0)
        return result;
    auto context = read<void *>(args, 0x10);
    if (!context || read<unsigned char>(context, 8) != 0)
        return result;
    const auto record = read<const unsigned char *>(context, 0x18);
    if (!record)
        return result;
    Camera camera;
    std::memcpy(camera.view, record + 0x100, sizeof(camera.view));
    std::memcpy(camera.projection, record + 0x160, sizeof(camera.projection));
    camera.near = read<float>(record, 0x1E0); camera.far = read<float>(record, 0x1E4);
    if (render_policy::valid(camera))
        draw(read<void *>(args, 0), read<void *>(args, 8), context, camera);
    else
        refuse(24);
    return result;
}
}
bool install(std::uintptr_t mainBase) {
    base = mainBase;
    constexpr totk::engine::CodeSite guards[]{
        {0x7F91E0, 0xD10283FF, 0xA9047BFD}, {0xA5B0B4, 0xD101C3FF, 0xA9017BFD},
        {0x95F604, 0xD103C3FF, 0xA9097BFD}, {0x95EF6C, 0xD10143FF, 0xA9017BFD},
        {0x74C19C, 0x52802028, 0xD280A0A9}, {0xC5A6FC, 0xA9BE7BFD, 0xA9014FF4},
        {0x756A08, 0xD10243FF, 0xA9037BFD}, {0x962C18, 0xF942A000, 0xB4000080},
        {0xC4B7AC, 0xA9BE7BFD, 0xA9014FF4},
    };
    return totk::engine::codeMatches(base, guards, "remote-bomb", "RENDER_GUARD") &&
           totk::render::installPfxHook(base, pfx, previous, "remote-bomb");
}
// The HUD icon's centre and half size in the game's 1280x720 layout.
#ifdef REMOTE_BOMB_HUD_BESIDE_STASIS
constexpr auto kHudRect = totk::render::ability_hud::rect(1);
#else
constexpr float kHudRect[4]{515.f, 510.f, 40.f, 40.f};
#endif
namespace {
struct alignas(16) HudUniforms {
    float rect[4]{}, gauge[4]{}, tint[4]{1,1,1,1};
};
void drawGauge(std::uintptr_t module, std::uintptr_t args, const HudUniforms& uniform, unsigned image) {
    if (!module || !args)
        return;
    if (!initialized || samplerId < 0) {
        if (!(refusals & (1u << 28)))
            Logging.Log("[remote-bomb] HUD_WAIT initialized=%u sampler=%d", unsigned(initialized), samplerId);
        refusals |= 1u << 28;
        return;
    }
    const auto pointer = [](std::uintptr_t address) {
        return totk::engine::isPlausibleStringAddress(address)
                   ? read<std::uintptr_t>(reinterpret_cast<const void *>(address), 0) : 0;
    };
    const auto info = pointer(args);
    const auto context = info ? pointer(info + 0x58) : 0;
    const auto secondary = info ? pointer(info + 0x20) : 0;
    const auto target = secondary ? secondary : info ? pointer(info + 0x18) : 0;
    const auto viewPort = info ? pointer(info + 0x48) : 0;
    const auto complete = target ? pointer(pointer(target) + 0x28) : 0;
    auto *command = context ? read<NVNcommandBuffer *>(reinterpret_cast<void *>(context), 0xB8) : nullptr;
    if (!context || !target || !viewPort || !command || !complete) {
        refuse(25);
        return;
    }
    if (!allocatorReady()) {
        refuse(26);
        return;
    }
    const auto block = uniformBlock(uniform);
    if (!block) {
        refuse(27);
        return;
    }
    auto *drawContext = reinterpret_cast<void *>(context);
    auto *buffer = reinterpret_cast<void *>(target);
    alignas(8) unsigned char graphics[128]{}, bindScratch[40]{};
    resetState(graphics);
    graphics[0] = graphics[1] = graphics[101] = 0;
    blend(graphics, 1, NVN_BLEND_FUNC_SRC_ALPHA, NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA);
    applyState(graphics, drawContext);
    native<void (*)(void *, void *, void *)>(0x74F09C)(reinterpret_cast<void *>(viewPort), drawContext, buffer);
    native<void (*)(void *, void *, void *, unsigned)>(0x7B9274)(buffer, drawContext, bindScratch, 0);
    bindProgram(command, &hudProgram, 63);
    bindUniforms(command, block, sizeof(uniform));
    const unsigned images[]{image, kHudTextureStart + 4, kHudTextureStart + 5};
    barrier(command, NVN_BARRIER_INVALIDATE_TEXTURE_DESCRIPTOR_BIT);
    for (unsigned i = 0; i < 3; ++i)
        bindTexture(command, NVN_SHADER_STAGE_FRAGMENT, i,
                    textureHandle(device, textureIds[images[i]], samplerId));
    drawArrays(command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 6);
    reinterpret_cast<void (*)(void *, void *)>(complete)(buffer, drawContext);
    native<void (*)(void *, void *)>(0xC4B7AC)(buffer, drawContext);
    applyState(reinterpret_cast<void *>(module + 0x110), drawContext);
}
} // namespace
void drawHud(std::uintptr_t module, std::uintptr_t args, hud_policy::View view, float alpha) {
    if (!view.visible) return;
    HudUniforms uniform;
    std::copy(std::begin(kHudRect),std::end(kHudRect),uniform.rect);
    uniform.gauge[0] = view.progress;
    uniform.gauge[1] = view.action == hud_policy::Action::Recharge ? 1.f : 0.f;
    uniform.gauge[2] = 1.7f;
    uniform.tint[3] = std::clamp(alpha,0.f,1.f);
    const unsigned icon = index(view.shape) + (view.upgrade == Upgrade::Plus ? 2 : 0);
    drawGauge(module,args,uniform,kHudTextureStart+icon);
}
#ifdef BOTW_ABILITIES_CHAMPIONS
void drawExtraHud(std::uintptr_t module, std::uintptr_t args) {
    const auto* extension = totk::render::hudExtension();
    totk::render::HudFrame frame;
    if (!extension || !extension->read(frame)) return;
    for (unsigned i = 0; i < std::min(frame.count,totk::render::kExtraHudIcons); ++i) {
        const auto& item = frame.items[i];
        if (item.icon >= totk::render::kExtraHudIcons || item.capacity > 6 || item.charges > item.capacity) continue;
        HudUniforms uniform;
        std::copy(item.rect.begin(),item.rect.end(),uniform.rect);
        std::copy(item.tint.begin(),item.tint.end(),uniform.tint);
        uniform.gauge[0] = item.progress;
        uniform.gauge[1] = item.recharging ? 1.f : 0.f;
        uniform.gauge[2] = 1.4f;
        uniform.gauge[3] = -float(1+item.charges+8*item.capacity);
        drawGauge(module,args,uniform,kBaseTextureCount+item.icon);
    }
}
#endif
}
