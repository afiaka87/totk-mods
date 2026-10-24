// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Draws the Stasis effects after the opaque scene, from the game's effect draw pass.
#include "StasisRender.hpp"
#include "StasisProjection.hpp"
#include "StasisSignals.hpp"
#include "StasisSurface.hpp"
#include "StasisRenderRoute.hpp"
#include "StasisShaders.hpp"
#include "totk/render/GlowTargets.hpp"
#include "totk/render/PfxHook.hpp"
#include <lib.hpp>
#include <nvn/nvn.h>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace stasis::render {
namespace {
// GraphicsContext stores depth flags, a target mask and six blend bytes per target.
constexpr std::size_t kStateDepthTest = 0;
constexpr std::size_t kStateDepthWrite = 1;
constexpr std::size_t kStateBlendTargets = 4;
constexpr std::size_t kStateBlendSrcRgb = 40;
constexpr std::size_t kStateBlendSrcAlpha = 41;
constexpr std::size_t kStateBlendDstRgb = 42;
constexpr std::size_t kStateBlendDstAlpha = 43;
constexpr std::size_t kStateBlendEquationRgb = 44;
constexpr std::size_t kStateBlendEquationAlpha = 45;

// Render camera record, reached from the draw context; projection 4x4 and view 3x4, row major.
constexpr std::size_t kContextCamera = 0x18;

// Scene render-target record, for the depth texture.
constexpr std::size_t kBuffersRecord = 0x10;

bool g_ready{}, g_attempted{}, g_programReady{}, g_programAttempted{};
unsigned g_lastRefusal{};
bool g_loggedScissorMissing{};

NVNbufferAddress g_codeAddress{};
struct ShaderBlob {
    const unsigned char *code;
    std::size_t bytes;
    const unsigned char *control;
};
#define BLOB(name) {shaders::name##Code, sizeof(shaders::name##Code), shaders::name##Control}
constexpr ShaderBlob kShaders[]{BLOB(vert),          BLOB(frag),          BLOB(chainvert),
                                BLOB(chainfrag),     BLOB(arrowvert),     BLOB(arrowfrag),
                                BLOB(particlesvert), BLOB(particlesfrag), BLOB(burst)
#if !defined(STASIS_NATIVE_BLOOM) || !STASIS_NATIVE_BLOOM
                                , BLOB(blur), BLOB(composite), BLOB(downsample)
#endif
};
#undef BLOB
constexpr std::size_t codeEnd() {
    std::size_t end = 0;
    for (const auto &shader : kShaders)
        end = ((end + 255) & ~std::size_t(255)) + shader.bytes;
    return end;
}
constexpr auto kCodeEnd = codeEnd();
constexpr std::size_t kCodePoolBytes = (kCodeEnd + 16384 + 4095) & ~std::size_t(4095);
static_assert(kCodePoolBytes <= 262144, "shader code pool cap");
alignas(4096) unsigned char g_codeMemory[kCodePoolBytes]{};
alignas(8) NVNmemoryPool g_codePool{};
constexpr unsigned kShaderCount = sizeof(kShaders) / sizeof(kShaders[0]);
std::size_t g_codeOffsets[kShaderCount]{};
enum Pass { Surface, Chain, Arrow, Particle, Burst, Blur, Composite, Downsample, PassCount };
alignas(8) NVNprogram g_programs[PassCount]{};
// Fixed storage, shared with other glow passes in the module, avoids reallocating render textures still referenced by GPU work. Explicit pixel fetches use only the active viewport.
constexpr unsigned kMaxWidth = totk::render::kGlowSceneWidth, kMaxHeight = totk::render::kGlowSceneHeight;
const totk::render::GlowTargets *g_targets{};
PFNNVNCOMMANDBUFFERSETRENDERTARGETSPROC g_setTargets{};
PFNNVNCOMMANDBUFFERSETVIEWPORTPROC g_setViewport{};
PFNNVNCOMMANDBUFFERCLEARCOLORPROC g_clearColor{};
PFNNVNCOMMANDBUFFERBARRIERPROC g_barrier{};
PFNNVNCOMMANDBUFFERBINDTEXTUREPROC g_bindTexture{};
PFNNVNDEVICEGETTEXTUREHANDLEPROC g_textureHandle{};
NVNdevice *g_device{};

PFNNVNDEVICEGETPROCADDRESSPROC g_getProc{};
PFNNVNCOMMANDBUFFERBINDPROGRAMPROC g_bindProgram{};
PFNNVNCOMMANDBUFFERDRAWARRAYSPROC g_drawArrays{};
PFNNVNCOMMANDBUFFERBINDUNIFORMBUFFERPROC g_bindUniform{};
PFNNVNCOMMANDBUFFERSETSCISSORPROC g_setScissor{};

// Scene, all-model and hovered-model depths. Location 5 avoids the bloom samplers.
alignas(8) unsigned char g_samplerBindings[128]{};
alignas(8) std::uint32_t g_samplerLocation[12]{0xffff00ff, 0, 0, 0, 0xffff01ff, 0, 0, 0,
                                            0xffff05ff, 0, 0, 0};

template <class T> T read(const void *p, std::size_t offset) {
    T value{};
    if (p)
        std::memcpy(&value, static_cast<const unsigned char *>(p) + offset, sizeof(T));
    return value;
}

template <class T> void write(void *p, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char *>(p) + offset, &value, sizeof(T));
}

using engine::call;

// The object a global slot points to.
void *variable(std::uintptr_t offset) {
    return read<void *>(reinterpret_cast<void *>(engine::moduleBase), offset);
}

// Log each render failure once.
std::uint32_t g_refusalsLogged{};

void refuse(unsigned reason, unsigned detail = 0) {
    g_lastRefusal = reason;
    if (reason >= 32 || (g_refusalsLogged & (1u << reason)))
        return;
    g_refusalsLogged |= 1u << reason;
    Logging.Log("[stasis] SHADER refused reason=%u detail=%u", reason, detail);
}

template <class F> bool resolve(F &target, const char *name) {
    target = reinterpret_cast<F>(g_getProc(g_device, name));
    if (!target)
        Logging.Log("[stasis] SHADER missing NVN function %s", name);
    return target != nullptr;
}

unsigned depthSlot(unsigned flags) {
    return (flags & 1) ? unsigned(0x700) : (flags & 2) ? unsigned(0xe40) : 0;
}

// GPU setup, done lazily on the first draw callback, once the device exists.

bool initializeGpuStorage() {
    if (g_ready)
        return true;
    if (g_attempted)
        return false;
    auto *graphics = variable(offsets::kGraphicsDevice);
    g_getProc =
        reinterpret_cast<PFNNVNDEVICEGETPROCADDRESSPROC>(variable(offsets::kNvnGetProcAddress));
    if (!graphics || !g_getProc) {
        refuse(1);
        return false;
    }
    g_device = read<NVNdevice *>(graphics, 0x30);
    if (!g_device) {
        refuse(2);
        return false;
    }
    g_attempted = true;

    PFNNVNMEMORYPOOLBUILDERSETDEFAULTSPROC builderDefaults{};
    PFNNVNMEMORYPOOLBUILDERSETDEVICEPROC builderDevice{};
    PFNNVNMEMORYPOOLBUILDERSETSTORAGEPROC builderStorage{};
    PFNNVNMEMORYPOOLBUILDERSETFLAGSPROC builderFlags{};
    PFNNVNMEMORYPOOLINITIALIZEPROC poolInitialize{};
    PFNNVNMEMORYPOOLFLUSHMAPPEDRANGEPROC poolFlush{};
    PFNNVNMEMORYPOOLGETBUFFERADDRESSPROC poolAddress{};
    PFNNVNDEVICEGETINTEGERPROC getInteger{};
    if (!resolve(builderDefaults, "nvnMemoryPoolBuilderSetDefaults") ||
        !resolve(builderDevice, "nvnMemoryPoolBuilderSetDevice") ||
        !resolve(builderStorage, "nvnMemoryPoolBuilderSetStorage") ||
        !resolve(builderFlags, "nvnMemoryPoolBuilderSetFlags") ||
        !resolve(poolInitialize, "nvnMemoryPoolInitialize") ||
        !resolve(poolFlush, "nvnMemoryPoolFlushMappedRange") ||
        !resolve(poolAddress, "nvnMemoryPoolGetBufferAddress") ||
        !resolve(getInteger, "nvnDeviceGetInteger") ||
        !resolve(g_bindProgram, "nvnCommandBufferBindProgram") ||
        !resolve(g_drawArrays, "nvnCommandBufferDrawArrays") ||
        !resolve(g_bindUniform, "nvnCommandBufferBindUniformBuffer") ||
        !resolve(g_setTargets, "nvnCommandBufferSetRenderTargets") ||
        !resolve(g_setViewport, "nvnCommandBufferSetViewport") ||
        !resolve(g_clearColor, "nvnCommandBufferClearColor") ||
        !resolve(g_barrier, "nvnCommandBufferBarrier") ||
        !resolve(g_bindTexture, "nvnCommandBufferBindTexture") ||
        !resolve(g_textureHandle, "nvnDeviceGetTextureHandle")) {
        refuse(3);
        return false;
    }
    // Optional: without it the pass draws over the whole screen.
    if (!resolve(g_setScissor, "nvnCommandBufferSetScissor") && !g_loggedScissorMissing) {
        g_loggedScissorMissing = true;
        Logging.Log("[stasis] SHADER no scissor entry point; drawing fullscreen");
    }

    int padding = -1;
    getInteger(g_device, NVN_DEVICE_INFO_SHADER_CODE_MEMORY_POOL_PADDING_SIZE, &padding);
    if (padding < 0 || kCodeEnd + padding > sizeof(g_codeMemory)) {
        refuse(4, static_cast<unsigned>(padding));
        return false;
    }
    std::size_t offset = 0;
    for (unsigned i = 0; i < kShaderCount; ++i) {
        offset = (offset + 255) & ~std::size_t(255);
        g_codeOffsets[i] = offset;
        std::memcpy(g_codeMemory + offset, kShaders[i].code, kShaders[i].bytes);
        offset += kShaders[i].bytes;
    }

    alignas(8) NVNmemoryPoolBuilder builder{};
    builderDefaults(&builder);
    builderDevice(&builder, g_device);
    builderStorage(&builder, g_codeMemory, sizeof(g_codeMemory));
    builderFlags(&builder, NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED |
                               NVN_MEMORY_POOL_FLAGS_SHADER_CODE);
    if (!poolInitialize(&g_codePool, &builder)) {
        refuse(5);
        return false;
    }
    poolFlush(&g_codePool, 0, sizeof(g_codeMemory));
    g_codeAddress = poolAddress(&g_codePool);
    if (!g_codeAddress) {
        refuse(6);
        return false;
    }

    write<std::uint32_t>(g_samplerBindings, 0x68, 3);
    write<const void *>(g_samplerBindings, 0x70, g_samplerLocation);
    if (!g_setScissor) {
        refuse(3);
        return false;
    }
    if constexpr (!kNativeBloom) {
        auto *manager = read<void *>(variable(offsets::kNvnManagerSlot), 0);
        if (!manager) {
            refuse(22);
            return false;
        }
        g_targets = totk::render::glowTargets(
            g_device, g_getProc, manager,
            call<totk::render::RegisterTexture>(offsets::kNVNMgrRegisterTexture), "stasis");
        if (!g_targets)
            refuse(21);
    }
    g_ready = true;
    return true;
}

bool initializeProgram() {
    if (g_programReady)
        return true;
    if (g_programAttempted || !initializeGpuStorage())
        return false;
    g_programAttempted = true;
    PFNNVNPROGRAMINITIALIZEPROC programInitialize{};
    PFNNVNPROGRAMSETSHADERSPROC programSetShaders{};
    if (!resolve(programInitialize, "nvnProgramInitialize") ||
        !resolve(programSetShaders, "nvnProgramSetShaders")) {
        refuse(3);
        return false;
    }
    constexpr unsigned indices[PassCount][2]{{0, 1}, {2, 3}, {4, 5},  {6, 7},
                                             {0, 8}, {0, 9}, {0, 10}, {0, 11}};
    constexpr unsigned programCount = kNativeBloom ? unsigned(Blur) : unsigned(PassCount);
    for (unsigned i = 0; i < programCount; ++i) {
        if (!programInitialize(&g_programs[i], g_device)) {
            refuse(7, i);
            return false;
        }
        const unsigned v = indices[i][0], f = indices[i][1];
        const NVNshaderData stages[]{{g_codeAddress + g_codeOffsets[v], kShaders[v].control},
                                     {g_codeAddress + g_codeOffsets[f], kShaders[f].control}};
        if (!programSetShaders(&g_programs[i], 2, stages)) {
            refuse(8, i);
            return false;
        }
    }
    g_programReady = true;
    Logging.Log("[stasis] SHADER program linked");
    return true;
}

bool readCamera(const void *context, Camera &camera) {
    const auto *record = read<const unsigned char *>(context, kContextCamera);
    if (!record)
        return false;
    std::memcpy(camera.proj, record + 0x160, sizeof(camera.proj));
    std::memcpy(camera.view, record + 0x100, sizeof(camera.view));
    camera.nearMetres = read<float>(record, 0x1e0);
    camera.farMetres = read<float>(record, 0x1e4);
    for (float value : camera.proj)
        if (!std::isfinite(value))
            return false;
    for (float value : camera.view)
        if (!std::isfinite(value))
            return false;
    return std::isfinite(camera.nearMetres) && std::isfinite(camera.farMetres) &&
           camera.nearMetres > 0.0f && camera.farMetres > camera.nearMetres;
}

void draw(void *drawContext, void *scene, void *context, bool useSurface = false,
          const void *maskBuffers = nullptr, DrawPart part = DrawPart::Complete) {
    Camera camera{};
    if (context && readCamera(context, camera))
        engine::publishCameraRay(cameraRay(camera));
    else
        engine::publishCameraRay({});
    Presentation snapshot{};
    if (!engine::presentation(snapshot))
        return;
    if (!snapshot.aiming && !snapshot.frozen && snapshot.cooldownFrames <= 0.0f &&
        snapshot.releaseFlash <= 0.0f && !snapshot.trail.visible())
        return;
    if (!drawContext || (!scene && !maskBuffers) || !context || !read<void *>(context, 0x540) ||
        !read<void *>(context, 0x548)) {
        refuse(9);
        return;
    }
    auto *command = read<NVNcommandBuffer *>(drawContext, 0xb8);
    if (!command) {
        refuse(10);
        return;
    }

    // Borrow the scene depth texture.
    const auto *buffers = maskBuffers ? maskBuffers : read<const void *>(scene, 0x1ec0);
    if (!buffers || read<unsigned>(buffers, 8) < 1) {
        refuse(11);
        return;
    }
    auto *record = read<unsigned char *>(buffers, kBuffersRecord);
    if (!record) {
        refuse(12);
        return;
    }
    const auto targetFlags = read<unsigned>(record, 8);
    const auto slot = depthSlot(targetFlags);
    if (!slot) {
        refuse(13, targetFlags);
        return;
    }
    void *sampler = record + slot;
    const unsigned width = read<std::uint16_t>(sampler, 0x30);
    const unsigned height = read<std::uint16_t>(sampler, 0x30 + 2);
    if (width < 16 || height < 16 || width > 8192 || height > 8192) {
        refuse(14, width);
        return;
    }
    // A frame larger than the glow images is drawn without bloom.
    bool bloom = !kNativeBloom && width <= kMaxWidth && height <= kMaxHeight;
    if (!readCamera(context, camera)) {
        refuse(15);
        return;
    }
    camera.width = static_cast<float>(width);
    camera.height = static_cast<float>(height);

    Uniforms uniforms{};
    buildUniforms(camera, snapshot, uniforms);
    uniforms.resources[3] = static_cast<float>(part);
    void *modelDepth = nullptr;
    void *hoverDepth = nullptr;
    bool masked = useSurface && surface::masks(modelDepth, hoverDepth);
    if (masked) {
        const auto maskWidth = read<std::uint16_t>(modelDepth, 48);
        const auto maskHeight = read<std::uint16_t>(modelDepth, 50);
        if (maskWidth < 16 || maskHeight < 16 || maskWidth > 8192 || maskHeight > 8192) {
            refuse(20, (unsigned(maskWidth) << 16) | maskHeight);
            masked = false;
        } else {
            // The pinned NVN compiler misbinds textureSize at nonzero bindings.
            uniforms.maskDimensions[0] = float(maskWidth);
            uniforms.maskDimensions[1] = float(maskHeight);
            uniforms.maskDimensions[2] = hoverDepth ? 1.0f : 0.0f;
        }
    }
    if (useSurface && !masked)
        refuse(28, static_cast<unsigned>(part));
    uniforms.selectedMax[3] = masked ? 1.0f : 0.0f;
    if (!initializeProgram())
        return;

    // Uniform blocks come from the game's per-draw allocator.
    const auto *allocator = variable(offsets::kUniformAllocator);
    if (!allocator || !read<void *>(allocator, 24) || !read<unsigned>(allocator, 72)) {
        refuse(17);
        return;
    }
    // Allocate all variants before binding a private framebuffer. An allocation failure leaves the game's target and render state untouched.
    constexpr unsigned blockCount = kNativeBloom ? 2 : 10;
    void *blocks[blockCount]{};
    Uniforms variants[blockCount];
    for (auto &variant : variants)
        variant = uniforms;
    variants[1].effects[3] = 1;
    const unsigned glowW = totk::render::kGlowWidth, glowH = totk::render::kGlowHeight;
    const unsigned halfW = (glowW+1)/2, halfH = (glowH+1)/2, wideW = (glowW+3)/4, wideH = (glowH+3)/4;
    const float kernelX = float(glowW)/width, kernelY = float(glowH)/height;
    const float blurParameters[8][4]{
        {float(glowW), float(glowH), 2, 0}, {float(halfW), float(halfH), kernelX, 0},
        {float(halfW), float(halfH), 0, kernelY},  {float(halfW), float(halfH), 2, 0},
        {float(wideW), float(wideH), kernelX, 0},  {float(wideW), float(wideH), 0, kernelY},
        {float(wideW), float(wideH), kernelX, 0},  {float(wideW), float(wideH), 0, kernelY}};
    if constexpr (!kNativeBloom)
        for (unsigned i = 0; i < 8; ++i)
            std::memcpy(variants[i + 2].pipeline, blurParameters[i], sizeof(blurParameters[i]));
    for (unsigned i = 0; i < blockCount; ++i) {
        call<void (*)(void **, const void *, std::size_t)>(offsets::kUtlCreateDynamicUniformBlock)(
            &blocks[i], &variants[i], sizeof(Uniforms));
        if (!blocks[i] || !read<std::uint64_t>(blocks[i], 0x78)) {
            refuse(18, i);
            return;
        }
    }
    bool sampled = call<bool (*)(void *, void *, unsigned, void *)>(
        offsets::kShaderProgramActivateSampler)(g_samplerBindings, drawContext, 0, sampler);
    alignas(8) unsigned char maskSampler[240]{};
    alignas(8) unsigned char hoverSampler[240]{};
    if (masked) {
        call<void (*)(void *)>(offsets::kSamplerConstruct)(maskSampler);
        call<void (*)(void *, void *)>(offsets::kSamplerApplyTexture)(maskSampler, modelDepth);
        sampled = call<bool (*)(void *, void *, unsigned, void *)>(
                      offsets::kShaderProgramActivateSampler)(g_samplerBindings, drawContext, 1,
                                                              maskSampler) &&
                  sampled;
    } else {
        sampled = call<bool (*)(void *, void *, unsigned, void *)>(
                      offsets::kShaderProgramActivateSampler)(g_samplerBindings, drawContext, 1,
                                                              sampler) &&
                  sampled;
    }
    if (masked && hoverDepth) {
        call<void (*)(void *)>(offsets::kSamplerConstruct)(hoverSampler);
        call<void (*)(void *, void *)>(offsets::kSamplerApplyTexture)(hoverSampler, hoverDepth);
    }
    sampled = call<bool (*)(void *, void *, unsigned, void *)>(
        offsets::kShaderProgramActivateSampler)(g_samplerBindings,drawContext,2,
            masked && hoverDepth ? hoverSampler : sampler) && sampled;
    alignas(8) unsigned char state[128]{};
    if (sampled) {
        const int samplerId = read<std::int16_t>(sampler, 234);
        const auto bindPrivate = [&](unsigned location, unsigned texture) {
            g_bindTexture(command, NVN_SHADER_STAGE_FRAGMENT, location,
                          g_textureHandle(g_device, g_targets->ids[texture], samplerId));
        };
        const auto stateFor = [&](unsigned blendMask, bool preserveAlpha) {
            call<void (*)(void *)>(offsets::kGraphicsContextGraphicsContext)(state);
            state[kStateDepthTest] = 0;
            state[kStateDepthWrite] = 0;
            write<unsigned>(state, kStateBlendTargets, blendMask);
            for (unsigned i = 0; i < 2; ++i) {
                const unsigned offset = i * 6;
                state[kStateBlendSrcRgb + offset] = NVN_BLEND_FUNC_ONE;
                state[kStateBlendSrcAlpha + offset] =
                    preserveAlpha ? NVN_BLEND_FUNC_ZERO : NVN_BLEND_FUNC_ONE;
                state[kStateBlendDstRgb + offset] = NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA;
                state[kStateBlendDstAlpha + offset] =
                    preserveAlpha ? NVN_BLEND_FUNC_ONE : NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA;
                state[kStateBlendEquationRgb + offset] = state[kStateBlendEquationAlpha + offset] =
                    NVN_BLEND_EQUATION_ADD;
            }
            call<void (*)(void *, void *)>(offsets::kGraphicsContextApply)(state, drawContext);
        };
        const auto bindBlock = [&](unsigned index) {
            for (const auto stage : {NVN_SHADER_STAGE_VERTEX, NVN_SHADER_STAGE_FRAGMENT})
                g_bindUniform(command, stage, 0, read<std::uint64_t>(blocks[index], 0x78),
                              read<unsigned>(blocks[index], 0x38));
        };
        const auto fullscreen = [&](Pass pass, unsigned block) {
            g_bindProgram(command, &g_programs[pass], 63);
            bindBlock(block);
            g_drawArrays(command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
        };
        const auto geometry = [&](Pass pass, int vertices, int instances, unsigned block) {
            g_bindProgram(command, &g_programs[pass], 63);
            bindBlock(block);
            g_drawArrays(command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, vertices * instances);
        };
        // Another glow pass in the module may still be sampling the shared targets.
        g_barrier(command, NVN_BARRIER_ORDER_FRAGMENTS_BIT | NVN_BARRIER_INVALIDATE_TEXTURE_BIT |
                               NVN_BARRIER_INVALIDATE_TEXTURE_DESCRIPTOR_BIT);
        // Native surfaces precede the separate reduced-resolution emission pass.
        call<void (*)(void *, void *)>(offsets::kModelRenderContextBindDefaultFrameBuffer)(
            context, drawContext);
        totk::render::GameFrame frame{};
        const bool framed = totk::render::gameFrame(engine::moduleBase, context, frame);
        if (!framed)
            refuse(27);
        else if (!kNativeBloom && !bloom)
            refuse(26, (width << 16) | height);
        bloom = bloom && framed && g_targets;
        if (framed) {
            g_setTargets(command, 1, &frame.texture, &frame.view, nullptr, nullptr);
            g_setViewport(command, 0, 0, width, height);
            g_setScissor(command, 0, 0, width, height);
            stateFor(1, true);
            const auto drawObjects = [&]() {
                fullscreen(Surface, 0);
                if (part == DrawPart::Hud)
                    return;
                if (snapshot.frozen || snapshot.releaseFlash > .24f)
                    geometry(Chain, 48 * 10 * 6, 6 * 46, 0);
                if (snapshot.frozen) {
                    if (uniforms.tip[3] > .5f)
                        geometry(Arrow, 6, 1, 0);
                    if (snapshot.activationAge >= 0 && snapshot.activationAge < 1)
                        geometry(Particle, 6, 48, 0);
                    fullscreen(Burst, 0);
                }
                if (snapshot.trail.visible()) {
                    geometry(Particle, 6, int(snapshot.trail.count * 7), 1);
                }
            };
            drawObjects();
            if (bloom) {
                const NVNtexture *targets[]{nullptr, &g_targets->images[totk::render::Bright]};
                g_setTargets(command, 2, targets, nullptr, nullptr, nullptr);
                g_setViewport(command, 0, 0, glowW, glowH);
                g_setScissor(command, 0, 0, glowW, glowH);
                const float zero[4]{};
                g_clearColor(command, 1, zero, 15);
                stateFor(2, false);
                drawObjects();
                // The fragment-order and texture invalidation barrier separates every render-to-texture write from its following sampling pass.
                const auto textureBarrier = [&]() {
                    g_barrier(command,
                              NVN_BARRIER_ORDER_FRAGMENTS_BIT | NVN_BARRIER_INVALIDATE_TEXTURE_BIT);
                };
                using namespace totk::render;
                stateFor(0, false);
                // Both downsamples come first: the wide chain starts from NarrowA, and NarrowB and the wide images reuse Bright's memory once it has been read.
                constexpr unsigned order[]{0, 3, 1, 2, 4, 5, 6, 7};
                constexpr GlowImage source[]{Bright, NarrowA, NarrowB, NarrowA, WideA, WideB, WideA, WideB},
                    destination[]{NarrowA, NarrowB, NarrowA, WideA, WideB, WideA, WideB, WideA};
                for (const unsigned i : order) {
                    textureBarrier();
                    bindPrivate(2, source[i]);
                    const NVNtexture *targetTexture = &g_targets->images[destination[i]];
                    g_setTargets(command, 1, &targetTexture, nullptr, nullptr, nullptr);
                    const auto bw = i < 3 ? halfW : wideW, bh = i < 3 ? halfH : wideH;
                    g_setViewport(command, 0, 0, bw, bh);
                    g_setScissor(command, 0, 0, bw, bh);
                    fullscreen((i == 0 || i == 3) ? Downsample : Blur, i + 2);
                }
                textureBarrier();
                // Adds the bloom to the game's frame.
                call<void (*)(void *, void *)>(offsets::kModelRenderContextBindDefaultFrameBuffer)(
                    context, drawContext);
                g_setViewport(command, 0, 0, width, height);
                g_setScissor(command, 0, 0, width, height);
                stateFor(1, true);
                bindPrivate(3, NarrowA);
                bindPrivate(4, WideA);
                fullscreen(Composite, 2);
            }
            g_lastRefusal = 0;
            if constexpr (kNativeBloom) {
                static unsigned reported{};
                const unsigned bit = part == DrawPart::Hud ? 1 : masked ? 2 : 4;
                if (!(reported & bit)) {
                    reported |= bit;
                    Logging.Log("[stasis] NATIVE_BLOOM_DRAW part=%u masked=%u width=%u height=%u",
                                unsigned(part), unsigned(masked), width, height);
                }
            }
        }
    } else
        refuse(19);
    if (masked) {
        call<void (*)(void *)>(offsets::kSamplerDestroyDescriptor)(maskSampler + 234);
        call<void (*)(void *)>(offsets::kSamplerDestroyTexture)(maskSampler + 88);
    }
    if (masked && hoverDepth) {
        call<void (*)(void *)>(offsets::kSamplerDestroyDescriptor)(hoverSampler + 234);
        call<void (*)(void *)>(offsets::kSamplerDestroyTexture)(hoverSampler + 88);
    }

    // Restore the game's render target and state.
    call<void (*)(void *, void *)>(offsets::kModelRenderContextUnbindDefaultFrameBuffer)(
        context, drawContext);
    auto *target = read<void *>(context, 0x540);
    call<void (*)(void *, void *)>(offsets::kRestoreRenderBuffers)(target, drawContext);
    call<void (*)(void *)>(offsets::kGraphicsContextGraphicsContext)(state);
    call<void (*)(void *, void *)>(offsets::kGraphicsContextApply)(state, drawContext);
}

struct DrawPfxHook {
    inline static totk::render::PfxCallback previous{};
    static std::uint64_t Callback(void *extension, void *args) {
        auto *context = args ? read<void *>(args, 0x10) : nullptr;
        const auto schedule = args && context ? nativeSchedule(read<unsigned>(args, 0x18),
            read<unsigned>(args, 0x1c), read<std::uint8_t>(context, 8)) : DrawSchedule{};
        if (kNativeBloom && schedule.beforeHud)
            draw(read<void *>(args, 0), read<void *>(args, 8), context, false, nullptr, DrawPart::Hud);
        const auto result = previous(extension, args);
        if constexpr (kNativeBloom) {
            if (schedule.afterWorld)
                draw(read<void *>(args, 0), read<void *>(args, 8), context,
                     surface::scheduled(), nullptr, DrawPart::World);
            return result;
        }
        // Phase 1, pass 0, view 0: one draw per frame after the opaque scene.
        if (!args || read<unsigned>(args, 0x1c) != 0 || read<unsigned>(args, 0x18) != 1)
            return result;
        if (!context || read<std::uint8_t>(context, 8) != 0)
            return result;
        Camera camera{};
        engine::publishCameraRay(readCamera(context, camera) ? cameraRay(camera) : Ray{});
        if (!surface::scheduled())
            draw(read<void *>(args, 0), read<void *>(args, 8), context);
        return result;
    }
};

} // namespace

void drawSurface(void *drawContext, void *context, void *buffers) {
    draw(drawContext, nullptr, context, true, buffers);
}

void install() {
    if (!totk::render::installPfxHook(engine::moduleBase, DrawPfxHook::Callback,
                                      DrawPfxHook::previous, "stasis"))
        return;
    Logging.Log("[stasis] shader installed pool=%u fragment=%u nativeBloom=%u", unsigned(sizeof(g_codeMemory)),
                unsigned(sizeof(shaders::fragCode)), unsigned(kNativeBloom));
}
} // namespace stasis::render
