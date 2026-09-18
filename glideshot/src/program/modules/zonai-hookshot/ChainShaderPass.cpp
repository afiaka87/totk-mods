// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Screen-space chain and reticle: one draw call, evaluated per pixel. The plumbing follows the
// survey depth pass; the blend state differs on purpose because the spine must darken.
#include "ChainShaderPass.hpp"

#include <lib.hpp>
#include <nvn/nvn.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "ChainPresentation.hpp"
#include "ChainProjection.hpp"
#include "ChainRenderer.hpp"
#include "ChainShaders.hpp"
#include "ChainVisual.hpp"

namespace zonai_hookshot::render {
namespace {
using pure::ChainStyleResolved;
using pure::ChainUniforms;
using pure::Vec3;

// Pinned engine addresses (TotK 1.2.1).

constexpr std::uintptr_t kDrawPfx = 0xc30c48;          // ModelSceneExtension::drawPfx_
constexpr std::uintptr_t kGetProcSlot = 0x46170f0;     // nvnDeviceGetProcAddress
constexpr std::uintptr_t kGraphicsSlot = 0x462ef88;    // graphics singleton; device at +0x30
constexpr std::uintptr_t kUniformAllocatorSlot = 0x46382e0;

constexpr std::uintptr_t kGraphicsContextCtor = 0x74c19c;   // sead::GraphicsContext::GraphicsContext
constexpr std::uintptr_t kGraphicsContextApply = 0x756a08;  // sead::GraphicsContext::apply
constexpr std::uintptr_t kCreateDynamicUniformBlock = 0x95f604;
constexpr std::uintptr_t kActivateSampler = 0x95ef6c;
constexpr std::uintptr_t kFramebufferBind = 0xc5a6fc;
constexpr std::uintptr_t kFramebufferUnbind = 0x962c18;
constexpr std::uintptr_t kFramebufferBarrier = 0xc4b7ac;

// sead::GraphicsContext offsets: bytes 0/1 depth test/write, dword 4 blend-enable mask, from byte
// 40 six bytes per target stored as srcRGB, srcAlpha, dstRGB, dstAlpha, eqRGB, eqAlpha.
constexpr std::size_t kStateDepthTest = 0;
constexpr std::size_t kStateDepthWrite = 1;
constexpr std::size_t kStateBlendTargets = 4;
constexpr std::size_t kStateBlendSrcRgb = 40;
constexpr std::size_t kStateBlendSrcAlpha = 41;
constexpr std::size_t kStateBlendDstRgb = 42;
constexpr std::size_t kStateBlendDstAlpha = 43;
constexpr std::size_t kStateBlendEquationRgb = 44;
constexpr std::size_t kStateBlendEquationAlpha = 45;

// Render camera record, reached from the draw context.
constexpr std::size_t kContextCamera = 0x18;
constexpr std::size_t kCameraProjection = 0x160;   // 4x4, row major
constexpr std::size_t kCameraView = 0x100;         // 3x4, row major
constexpr std::size_t kCameraNear = 0x1e0;
constexpr std::size_t kCameraFar = 0x1e4;

// Scene render-target record, for the depth texture.
constexpr std::size_t kSceneBuffers = 0x1ec0;
constexpr std::size_t kBuffersRecord = 0x10;

constexpr float kTau = pure::kChainTau;

// Style table

// Matte-black structure with restrained green; a refused preview reads red.
struct Style {
    float spine[3];
    float helixCore[3];
    float helixSecondary[3];
    float successCore[3];
    float reticleUnderlay[3];
    float spineWidth;
    float helixCoreWidth;
    float reticleWidth;
    float helixRadius;
    float phaseStep;
};

const Style kStyles[] = {
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0, 0, 0, 0, 0},
    // PreviewValid
    {{0.010f, 0.012f, 0.010f}, {0.18f, 0.90f, 0.58f}, {0.08f, 0.82f, 0.78f},
     {0.62f, 1.00f, 0.80f}, {0.010f, 0.020f, 0.014f},
     2.4f, 1.25f, 1.7f, 0.105f, 0.055f},
    // PreviewInvalid
    {{0.018f, 0.008f, 0.008f}, {0.95f, 0.20f, 0.16f}, {1.00f, 0.38f, 0.20f},
     {1.00f, 0.48f, 0.40f}, {0.050f, 0.008f, 0.008f},
     2.4f, 1.25f, 1.7f, 0.105f, 0.025f},
    // Launch
    {{0.008f, 0.010f, 0.009f}, {0.22f, 0.96f, 0.60f}, {0.06f, 0.86f, 0.82f},
     {0.70f, 1.00f, 0.84f}, {0.008f, 0.022f, 0.014f},
     3.2f, 2.2f, 2.15f, 0.24f, 0.16f},
    // Latched
    {{0.006f, 0.008f, 0.007f}, {0.16f, 0.91f, 0.53f}, {0.05f, 0.78f, 0.74f},
     {0.68f, 1.00f, 0.83f}, {0.006f, 0.020f, 0.012f},
     3.4f, 2.25f, 2.25f, 0.24f, 0.085f},
};
constexpr std::uint8_t kStyleCount = sizeof(kStyles) / sizeof(kStyles[0]);

std::uintptr_t g_base{};
float g_phaseRadians = 0.0f;

bool g_ready{}, g_attempted{}, g_programReady{}, g_programAttempted{};
unsigned g_lastRefusal{};
std::uint64_t g_draws{};
bool g_loggedFirstDraw{}, g_loggedScissorMissing{}, g_loggedDepthSource{};
unsigned g_lastWidth{}, g_lastHeight{}, g_lastSlot{};

NVNbufferAddress g_codeAddress{};
constexpr auto kFragmentOffset = (sizeof(shaders::vertCode) + 255) & ~std::size_t(255);
constexpr auto kCodeEnd = kFragmentOffset + sizeof(shaders::fragCode);
// The driver requires padding after the final shader program; queried at runtime and checked
// against this reservation.
constexpr std::size_t kCodePoolBytes = (kCodeEnd + 16384 + 4095) & ~std::size_t(4095);
static_assert(kCodeEnd <= 16384 && kCodePoolBytes <= 32768,
              "The chain shader is small on purpose; a large growth here wants review");

alignas(4096) unsigned char g_codeMemory[kCodePoolBytes]{};
alignas(8) NVNmemoryPool g_codePool{};
alignas(8) NVNprogram g_program{};
NVNdevice* g_device{};

PFNNVNDEVICEGETPROCADDRESSPROC g_getProc{};
PFNNVNCOMMANDBUFFERBINDPROGRAMPROC g_bindProgram{};
PFNNVNCOMMANDBUFFERDRAWARRAYSPROC g_drawArrays{};
PFNNVNCOMMANDBUFFERBINDUNIFORMBUFFERPROC g_bindUniform{};
PFNNVNCOMMANDBUFFERSETSCISSORPROC g_setScissor{};

// One-entry binding table for the borrowed depth sampler.
alignas(8) unsigned char g_samplerBindings[128]{};
alignas(8) std::uint32_t g_samplerLocation[4]{0xffff00ff, 0, 0, 0};

template <class T>
T read(const void* p, std::size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const unsigned char*>(p) + offset, sizeof(T));
    return value;
}

template <class T>
void write(void* p, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char*>(p) + offset, &value, sizeof(T));
}

template <class F>
F native(std::uintptr_t offset) {
    return reinterpret_cast<F>(g_base + offset);
}

// Engine singletons live in double-dereferenced slots.
void* global(std::uintptr_t offset) {
    const auto slot = read<std::uintptr_t>(reinterpret_cast<void*>(g_base), offset);
    const auto& module = exl::util::GetMainModuleInfo();
    if (slot < module.m_Text.m_Start || slot >= g_base + 0x6000000 || (slot & 7))
        return nullptr;
    return read<void*>(reinterpret_cast<void*>(slot), 0);
}

// Every guard logs why it fired once per distinct reason, so a blank screen is diagnosable without
// a second boot and the 4 MB log is not flooded.
std::uint32_t g_refusalsLogged{};

void refuse(unsigned reason, unsigned detail = 0) {
    g_lastRefusal = reason;
    if (reason >= 32 || (g_refusalsLogged & (1u << reason))) return;
    g_refusalsLogged |= 1u << reason;
    Logging.Log("[zonai-hookshot] CHAIN_SHADER refused reason=%u detail=%u draws=%llu",
                reason, detail, static_cast<unsigned long long>(g_draws));
}

template <class F>
bool resolve(F& target, const char* name) {
    target = reinterpret_cast<F>(g_getProc(g_device, name));
    if (!target) Logging.Log("[zonai-hookshot] CHAIN_SHADER missing NVN function %s", name);
    return target != nullptr;
}

constexpr unsigned depthSlot(unsigned flags) {
    return (flags & 1) ? 0x700 : (flags & 2) ? 0xe40 : 0;
}

// GPU setup, done lazily on the first draw callback, once the device exists.

bool initializeGpuStorage() {
    if (g_ready) return true;
    if (g_attempted) return false;
    auto* graphics = global(kGraphicsSlot);
    g_getProc = reinterpret_cast<PFNNVNDEVICEGETPROCADDRESSPROC>(global(kGetProcSlot));
    if (!graphics || !g_getProc) { refuse(1); return false; }
    g_device = read<NVNdevice*>(graphics, 0x30);
    if (!g_device) { refuse(2); return false; }
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
        !resolve(g_bindUniform, "nvnCommandBufferBindUniformBuffer")) {
        refuse(3);
        return false;
    }
    // Optional: without it the pass draws over the whole screen.
    if (!resolve(g_setScissor, "nvnCommandBufferSetScissor") && !g_loggedScissorMissing) {
        g_loggedScissorMissing = true;
        Logging.Log("[zonai-hookshot] CHAIN_SHADER no scissor entry point; drawing fullscreen");
    }

    int padding = -1;
    getInteger(g_device, NVN_DEVICE_INFO_SHADER_CODE_MEMORY_POOL_PADDING_SIZE, &padding);
    if (padding < 0 || kCodeEnd + padding > sizeof(g_codeMemory)) {
        refuse(4, static_cast<unsigned>(padding));
        return false;
    }
    std::memcpy(g_codeMemory, shaders::vertCode, sizeof(shaders::vertCode));
    std::memcpy(g_codeMemory + kFragmentOffset, shaders::fragCode, sizeof(shaders::fragCode));

    alignas(8) NVNmemoryPoolBuilder builder{};
    builderDefaults(&builder);
    builderDevice(&builder, g_device);
    builderStorage(&builder, g_codeMemory, sizeof(g_codeMemory));
    builderFlags(&builder, NVN_MEMORY_POOL_FLAGS_CPU_CACHED |
                           NVN_MEMORY_POOL_FLAGS_GPU_CACHED |
                           NVN_MEMORY_POOL_FLAGS_SHADER_CODE);
    if (!poolInitialize(&g_codePool, &builder)) { refuse(5); return false; }
    poolFlush(&g_codePool, 0, sizeof(g_codeMemory));
    g_codeAddress = poolAddress(&g_codePool);
    if (!g_codeAddress) { refuse(6); return false; }

    write<std::uint32_t>(g_samplerBindings, 0x68, 1);
    write<const void*>(g_samplerBindings, 0x70, g_samplerLocation);
    g_ready = true;
    Logging.Log("[zonai-hookshot] CHAIN_SHADER pool ready bytes=%u padding=%d vertex=%u fragment=%u",
                static_cast<unsigned>(sizeof(g_codeMemory)), padding,
                static_cast<unsigned>(sizeof(shaders::vertCode)),
                static_cast<unsigned>(sizeof(shaders::fragCode)));
    return true;
}

bool initializeProgram() {
    if (g_programReady) return true;
    if (g_programAttempted || !initializeGpuStorage()) return false;
    g_programAttempted = true;
    PFNNVNPROGRAMINITIALIZEPROC programInitialize{};
    PFNNVNPROGRAMSETSHADERSPROC programSetShaders{};
    if (!resolve(programInitialize, "nvnProgramInitialize") ||
        !resolve(programSetShaders, "nvnProgramSetShaders")) {
        refuse(3);
        return false;
    }
    if (!programInitialize(&g_program, g_device)) { refuse(7); return false; }
    const NVNshaderData stages[]{{g_codeAddress, shaders::vertControl},
                                 {g_codeAddress + kFragmentOffset, shaders::fragControl}};
    if (!programSetShaders(&g_program, 2, stages)) { refuse(8); return false; }
    g_programReady = true;
    Logging.Log("[zonai-hookshot] CHAIN_SHADER program linked");
    return true;
}

// Per-frame CPU work

Vec3 asVec3(const float* value) { return {value[0], value[1], value[2]}; }

void mixRgb(float* out, const float* from, const float* to, float amount) {
    for (int i = 0; i < 3; ++i) out[i] = from[i] + (to[i] - from[i]) * amount;
}

bool readCamera(const void* context, pure::CameraFrame& camera) {
    const auto* record = read<const unsigned char*>(context, kContextCamera);
    if (!record) return false;
    std::memcpy(camera.proj, record + kCameraProjection, sizeof(camera.proj));
    std::memcpy(camera.view, record + kCameraView, sizeof(camera.view));
    camera.nearMetres = read<float>(record, kCameraNear);
    camera.farMetres = read<float>(record, kCameraFar);
    for (float value : camera.proj)
        if (!std::isfinite(value)) return false;
    for (float value : camera.view)
        if (!std::isfinite(value)) return false;
    return std::isfinite(camera.nearMetres) && std::isfinite(camera.farMetres) &&
           camera.nearMetres > 0.0f && camera.farMetres > camera.nearMetres;
}

// Resolve the style table and latch pulse into the flat block; the shader never learns which state
// it draws.
void resolveStyle(const Snapshot& snapshot, float pulse, ChainStyleResolved& out) {
    const Style& style = kStyles[snapshot.style];
    mixRgb(out.coreARgb, style.helixCore, style.successCore, pulse);
    mixRgb(out.coreBRgb, style.helixSecondary, style.successCore, pulse);
    mixRgb(out.reticleRgb, style.helixCore, style.successCore, pulse);
    for (int i = 0; i < 3; ++i) {
        out.spineRgb[i] = style.spine[i];
        out.reticleUnderRgb[i] = style.reticleUnderlay[i];
    }
    out.spineCoverage = 1.0f;
    out.strandCoverage = 1.0f;
    out.reticleCoverage = 1.0f;
    out.reticleUnderCoverage = 1.0f;
    // Line-renderer widths are used as half widths for the spine and strands, since the shader
    // fades a pixel on each side (v0.6.0 boot: strands too thin).
    out.spineHalfPx = style.spineWidth * 0.75f;
    out.strandHalfPx = style.helixCoreWidth * (1.0f + 0.35f * pulse);
    out.ringHalfPx = style.reticleWidth * (1.0f + 0.60f * pulse) * 0.5f;
    out.underlayHalfPx = style.reticleWidth * (1.0f + 0.60f * pulse);
    out.reticleInnerScale = 0.46f;
    out.helixRadius = style.helixRadius;
    out.wavelength = pure::kHelixWavelength;
    // Negated to match the winding of the old line helix.
    out.phaseRadians = -g_phaseRadians;
}

void draw(void* drawContext, void* scene, void* context) {
    Snapshot snapshot{};
    if (!takeSnapshot(snapshot)) return;
    if (!snapshot.visible || snapshot.style == 0 || snapshot.style >= kStyleCount)
        return;

    if (!drawContext || !scene || !context || !read<void*>(context, 0x540) ||
        !read<void*>(context, 0x548)) {
        refuse(9);
        return;
    }
    auto* command = read<NVNcommandBuffer*>(drawContext, 0xb8);
    if (!command) { refuse(10); return; }

    // borrow the scene depth texture
    const auto* buffers = read<const void*>(scene, kSceneBuffers);
    if (!buffers || read<unsigned>(buffers, 8) < 1) { refuse(11); return; }
    auto* record = read<unsigned char*>(buffers, kBuffersRecord);
    if (!record) { refuse(12); return; }
    const auto targetFlags = read<unsigned>(record, 8);
    const auto slot = depthSlot(targetFlags);
    if (!slot) { refuse(13, targetFlags); return; }
    void* sampler = record + slot;
    const unsigned width = read<std::uint16_t>(sampler, 0x30);
    const unsigned height = read<std::uint16_t>(sampler, 0x32);
    if (width < 16 || height < 16 || width > 8192 || height > 8192) {
        refuse(14, width);
        return;
    }
    if (!g_loggedDepthSource || slot != g_lastSlot || width != g_lastWidth ||
        height != g_lastHeight) {
        g_loggedDepthSource = true;
        g_lastSlot = slot;
        g_lastWidth = width;
        g_lastHeight = height;
        Logging.Log("[zonai-hookshot] CHAIN_SHADER depth slot=%x flags=%x size=%ux%u",
                    slot, targetFlags, width, height);
    }

    pure::CameraFrame camera{};
    if (!readCamera(context, camera)) { refuse(15); return; }
    camera.width = static_cast<float>(width);
    camera.height = static_cast<float>(height);

    const float pulse = pure::latchFeedbackEnvelope(snapshot.latchFeedbackTicks);
    const Style& style = kStyles[snapshot.style];
    g_phaseRadians += style.phaseStep * (1.0f + 2.4f * pulse);
    if (g_phaseRadians > kTau) g_phaseRadians -= kTau;

    ChainStyleResolved resolved{};
    resolveStyle(snapshot, pulse, resolved);

    pure::ChainGeometry geometry{};
    geometry.nearPoint = asVec3(snapshot.nearPoint);
    geometry.farPoint = asVec3(snapshot.farPoint);
    geometry.reticlePoint = asVec3(snapshot.reticlePoint);
    geometry.bodyVisible = pure::chainBodyVisible(
        static_cast<pure::ChainStyle>(snapshot.style));
    geometry.reticleVisible = true;
    geometry.reticleWorldSize =
        pure::reticleWorldSize(pure::distance(geometry.nearPoint, geometry.reticlePoint)) *
        (1.0f + 0.32f * pulse);

    ChainUniforms uniforms{};
    if (!pure::buildChainUniforms(camera, geometry, resolved, uniforms)) {
        refuse(16);
        return;
    }
    if (!initializeProgram()) return;

    // uniform block from the engine's per-draw allocator
    const auto* allocator = global(kUniformAllocatorSlot);
    if (!allocator || !read<void*>(allocator, 24) || !read<unsigned>(allocator, 72)) {
        refuse(17);
        return;
    }
    void* block = nullptr;
    native<void (*)(void**, const void*, std::size_t)>(kCreateDynamicUniformBlock)(
        &block, &uniforms, sizeof(uniforms));
    if (!block || !read<std::uint64_t>(block, 120)) { refuse(18); return; }

    // Premultiplied over, not additive: the spine and reticle underlay must be able to darken.
    alignas(8) unsigned char state[128]{};
    native<void (*)(void*)>(kGraphicsContextCtor)(state);
    state[kStateDepthTest] = 0;      // the shader does its own depth compare
    state[kStateDepthWrite] = 0;
    write<unsigned>(state, kStateBlendTargets, 1);   // blend enabled, target 0
    state[kStateBlendSrcRgb] = NVN_BLEND_FUNC_ONE;
    state[kStateBlendSrcAlpha] = NVN_BLEND_FUNC_ZERO;
    state[kStateBlendDstRgb] = NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA;
    state[kStateBlendDstAlpha] = NVN_BLEND_FUNC_ONE;   // leave destination alpha alone
    state[kStateBlendEquationRgb] = NVN_BLEND_EQUATION_ADD;
    state[kStateBlendEquationAlpha] = NVN_BLEND_EQUATION_ADD;
    native<void (*)(void*, void*)>(kGraphicsContextApply)(state, drawContext);
    native<void (*)(void*, void*)>(kFramebufferBind)(context, drawContext);

    const bool sampled = native<bool (*)(void*, void*, unsigned, void*)>(kActivateSampler)(
        g_samplerBindings, drawContext, 0, sampler);
    if (sampled) {
        g_bindProgram(command, &g_program, 63);
        // The fragment uniform block binds at NVN index 0.
        g_bindUniform(command, NVN_SHADER_STAGE_FRAGMENT, 0,
                      read<std::uint64_t>(block, 120), read<unsigned>(block, 56));

        // Scissor to the bounding box of the endpoints and reticle; the union with its vertical mirror is submitted
        // because the scissor origin convention is not established, and both bound the work.
        const pure::ScissorBox box = pure::chainScissorBox(uniforms);
        const int mirrorY = static_cast<int>(height) - (box.y + box.height);
        const int lowY = mirrorY < box.y ? mirrorY : box.y;
        const int highY = mirrorY + box.height > box.y + box.height
            ? mirrorY + box.height
            : box.y + box.height;
        const int clampedY = lowY < 0 ? 0 : lowY;
        const int clampedHeight =
            (highY > static_cast<int>(height) ? static_cast<int>(height) : highY) - clampedY;
        const bool scissored =
            g_setScissor && box.width > 0 && box.height > 0 && clampedHeight > 0;
        if (scissored) g_setScissor(command, box.x, clampedY, box.width, clampedHeight);

        g_drawArrays(command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);

        if (scissored)
            g_setScissor(command, 0, 0, static_cast<int>(width), static_cast<int>(height));

        if (!g_loggedFirstDraw) {
            g_loggedFirstDraw = true;
            Logging.Log("[zonai-hookshot] CHAIN_SHADER first draw style=%u body=%d reticle=%d "
                        "near_px=%d,%d far_px=%d,%d span_cm=%d reticle_px=%d,%d r=%d "
                        "scissor=%d,%d,%dx%d px_scale=%d,%d",
                        static_cast<unsigned>(snapshot.style),
                        static_cast<int>(uniforms.flags[1]), static_cast<int>(uniforms.flags[2]),
                        static_cast<int>(uniforms.nearPoint[0]), static_cast<int>(uniforms.nearPoint[1]),
                        static_cast<int>(uniforms.farPoint[0]), static_cast<int>(uniforms.farPoint[1]),
                        static_cast<int>(uniforms.farPoint[3] * 100.0f),
                        static_cast<int>(uniforms.reticle[0]), static_cast<int>(uniforms.reticle[1]),
                        static_cast<int>(uniforms.reticle[3]),
                        box.x, clampedY, box.width, clampedHeight,
                        static_cast<int>(uniforms.pxScale[0]), static_cast<int>(uniforms.pxScale[1]));
        }
        ++g_draws;
        g_lastRefusal = 0;
    } else {
        refuse(19);
    }

    // put the frame back the way it was
    native<void (*)(void*, void*)>(kFramebufferUnbind)(context, drawContext);
    native<void (*)(void*, void*)>(kFramebufferBarrier)(read<void*>(context, 0x540), drawContext);
    native<void (*)(void*)>(kGraphicsContextCtor)(state);
    native<void (*)(void*, void*)>(kGraphicsContextApply)(state, drawContext);
}

HOOK_DEFINE_TRAMPOLINE(DrawPfxHook) {
    static std::uint64_t Callback(void* extension, void* args) {
        const auto result = Orig(extension, args);
        // Phase 1, pass 0, view 0: one draw per frame after the opaque scene.
        if (!args || read<unsigned>(args, 0x1c) != 0 || read<unsigned>(args, 0x18) != 1)
            return result;
        auto* context = read<void*>(args, 0x10);
        if (!context || read<std::uint8_t>(context, 8) != 0) return result;
        draw(read<void*>(args, 0), read<void*>(args, 8), context);
        return result;
    }
};

}  // namespace

void installShaderPass(std::uintptr_t mainBase) {
    g_base = mainBase;
    const auto* code = reinterpret_cast<const unsigned*>(mainBase + kDrawPfx);
    if (code[0] != 0xd104c3ff || code[1] != 0xa90d7bfd) {
        Logging.Log("[zonai-hookshot] CHAIN_SHADER unsupported hook bytes=%08x,%08x; pass disabled",
                    code[0], code[1]);
        return;
    }
    DrawPfxHook::InstallAtOffset(kDrawPfx);
    Logging.Log("[zonai-hookshot] CHAIN_SHADER installed pool=%u fragment=%u",
                static_cast<unsigned>(sizeof(g_codeMemory)),
                static_cast<unsigned>(sizeof(shaders::fragCode)));
}

}  // namespace zonai_hookshot::render
