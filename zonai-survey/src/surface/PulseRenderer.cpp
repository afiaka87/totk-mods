// SPDX-License-Identifier: MIT
#include "PulseRenderer.hpp"
#include "PulsePolicy.hpp"
#include "SurveyShaders.hpp"
#include "../pure/SurveyOptions.hpp"
#include "CameraHeading.hpp"
#include "AtlasGpu.hpp"
#include "AtlasRenderer.hpp"
#include <lib.hpp>
#include <nvn/nvn.h>
#include <atomic>
#include <cstring>
#include "totk/render/PfxHook.hpp"
#include "SurveyGameProfiles.hpp"

namespace zonai_survey::pulse {
namespace {

// Logged as numbers; keep the values stable.
enum Refusal : unsigned {
    kGraphicsMissing = 1, kDeviceMissing = 2, kDriverCallMissing = 3, kCodePadding = 4,
    kCodePool = 5, kProgramSetup = 6, kShaderInstall = 7, kDrawTarget = 8, kDepthBuffers = 9,
    kDepthRecord = 10, kDepthSlot = 11, kDepthSize = 12, kCamera = 13, kUniformAllocator = 14,
    kUniformBlock = 15, kSampler = 16, kCommandBuffer = 17,
};

std::uintptr_t g_base{};
std::atomic<unsigned> g_pulseSequence{};
std::atomic<float> g_origin[3]{}, g_heading[2]{};
#if SURVEY_CONSTRAINED
std::atomic<float> g_pulseRange{zonai_survey::options::nextRange()};
#endif
std::atomic<std::uint64_t> g_startTick{};
std::atomic<bool> g_anchorValid{};
unsigned g_lastRefusal{};
bool g_ready{}, g_attempted{}, g_programReady{}, g_programAttempted{};
NVNbufferAddress g_codeAddress{};
constexpr auto kFragmentOffset = (sizeof(shaders::vertCode) + 255) & ~std::size_t(255);
constexpr auto kCodeEnd=kFragmentOffset+sizeof(shaders::fragCode);
// The driver requires padding after the final shader program.
constexpr auto kCodePoolBytes=(kCodeEnd+16384+4095)&~std::size_t(4095);
static_assert(kCodeEnd<=49152 && kCodePoolBytes<=65536);
alignas(4096) unsigned char g_codeMemory[kCodePoolBytes]{};
alignas(8) NVNmemoryPool g_codePool{};
alignas(8) NVNprogram g_program{};
NVNdevice* g_device{};
PFNNVNDEVICEGETPROCADDRESSPROC g_getProc{};
PFNNVNCOMMANDBUFFERBINDPROGRAMPROC g_bindProgram{};
PFNNVNCOMMANDBUFFERDRAWARRAYSPROC g_drawArrays{};
PFNNVNCOMMANDBUFFERBINDUNIFORMBUFFERPROC g_bindUniform{};

alignas(8) unsigned char g_samplerBindings[128]{};
alignas(8) std::uint32_t g_samplerLocation[4]{0xffff00ff, 0, 0, 0};

template<class T> T read(const void* p, std::size_t offset) {
    T value{}; std::memcpy(&value, static_cast<const unsigned char*>(p) + offset, sizeof(T)); return value;
}
template<class T> void write(void* p, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char*>(p) + offset, &value, sizeof(T));
}
template<class F> F native(std::uintptr_t offset) { return reinterpret_cast<F>(g_base + offset); }
void* global(std::uintptr_t offset) {
    const auto variable = g_base + offset;
    if (variable < g_base || variable >= g_base + 0x6000000 || (variable & 7)) return nullptr;
    return read<void*>(reinterpret_cast<void*>(variable), 0);
}
void refuse(Refusal reason, unsigned detail = 0) {
    if (g_lastRefusal != reason) {
        g_lastRefusal = reason;
        Logging.Log("[survey-pulse] REFUSED reason=%u detail=%u\n", unsigned(reason), detail);
    }
}
template<class F> bool resolve(F& target, const char* name) {
    target = reinterpret_cast<F>(g_getProc(g_device, name));
    if (!target) Logging.Log("[survey-pulse] missing NVN function %s\n", name);
    return target != nullptr;
}

bool initializeGpuStorage() {
    if (g_ready) return true;
    if (g_attempted) return false;
    const auto& game = *zonai_survey::profiles::active;
    auto* graphics = global(game.variables.graphics);
    g_getProc = reinterpret_cast<PFNNVNDEVICEGETPROCADDRESSPROC>(global(game.variables.getProc));
    if (!graphics || !g_getProc) { refuse(kGraphicsMissing); return false; }
    g_device = read<NVNdevice*>(graphics, game.layout.deviceOffset);
    if (!g_device) { refuse(kDeviceMissing); return false; }
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
        !resolve(g_bindUniform, "nvnCommandBufferBindUniformBuffer")) { refuse(kDriverCallMissing); return false; }
    int padding = -1;
    getInteger(g_device, NVN_DEVICE_INFO_SHADER_CODE_MEMORY_POOL_PADDING_SIZE, &padding);
    if (padding < 0 || kCodeEnd + padding > sizeof(g_codeMemory)) {
        refuse(kCodePadding, unsigned(padding)); return false;
    }
    std::memcpy(g_codeMemory, shaders::vertCode, sizeof(shaders::vertCode));
    std::memcpy(g_codeMemory + kFragmentOffset, shaders::fragCode, sizeof(shaders::fragCode));
    alignas(8) NVNmemoryPoolBuilder builder{};
    builderDefaults(&builder);
    builderDevice(&builder, g_device);
    builderStorage(&builder, g_codeMemory, sizeof(g_codeMemory));
    builderFlags(&builder, NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED |
                           NVN_MEMORY_POOL_FLAGS_SHADER_CODE);
    if (!poolInitialize(&g_codePool, &builder)) { refuse(kCodePool); return false; }
    poolFlush(&g_codePool, 0, sizeof(g_codeMemory));
    g_codeAddress = poolAddress(&g_codePool);
    if (!g_codeAddress) { refuse(kProgramSetup); return false; }
    write<std::uint32_t>(g_samplerBindings, 0x68, 1);
    write<const void*>(g_samplerBindings, 0x70, g_samplerLocation);
    g_ready = true;
    return true;
}

bool initializeProgram() {
    if (g_programReady) return true;
    if (g_programAttempted || !initializeGpuStorage()) return false;
    g_programAttempted = true;
    PFNNVNPROGRAMINITIALIZEPROC programInitialize{};
    PFNNVNPROGRAMSETSHADERSPROC programSetShaders{};
    if (!resolve(programInitialize, "nvnProgramInitialize") ||
        !resolve(programSetShaders, "nvnProgramSetShaders")) { refuse(kDriverCallMissing); return false; }
    if (!programInitialize(&g_program, g_device)) { refuse(kProgramSetup); return false; }
    const NVNshaderData stages[]{{g_codeAddress, shaders::vertControl},
        {g_codeAddress + kFragmentOffset, shaders::fragControl}};
    if (!programSetShaders(&g_program, 2, stages)) { refuse(kShaderInstall); return false; }
    g_programReady = true;
    Logging.Log("[survey-pulse] GPU_READY\n");
    return true;
}

bool cameraUniforms(void* context, Uniforms& uniforms) {
    const auto& layout = zonai_survey::profiles::active->layout;
    const auto* camera = read<const unsigned char*>(context, 0x18);
    if (!camera) return false;
    float projection[16], view[16]{}, inverseView[16];
    std::memcpy(projection, camera + layout.cameraProjection, sizeof(projection));
    std::memcpy(view, camera + layout.cameraView, 12 * sizeof(float));
    view[15] = 1;
    if (!inverse4(projection, uniforms.inverseProjection) || !inverse4(view, inverseView)) return false;
    std::memcpy(uniforms.inverseView, inverseView, sizeof(uniforms.inverseView));

    uniforms.dimensions[2] = read<float>(camera, layout.cameraNear);
    uniforms.dimensions[3] = read<float>(camera, layout.cameraFar);
    const auto perspective = read<std::uint8_t>(camera, layout.cameraPerspective);
    uniforms.settings[2] = float(perspective);
    return perspective <= 1 && validClipRange(uniforms.dimensions[2], uniforms.dimensions[3]);
}

bool hasTarget(void* drawContext, void* context) {
    const auto& layout = zonai_survey::profiles::active->layout;
    return drawContext && context && read<void*>(context, layout.contextTarget) &&
           read<void*>(context, layout.contextViewport);
}

void draw(void* drawContext, void* scene, void* context) {
    const auto& game = *zonai_survey::profiles::active;
    const auto sequence = g_pulseSequence.load();
    Uniforms uniforms{};
    for (unsigned i = 0; i < 3; ++i) uniforms.scanOrigin[i] = g_origin[i].load();
    for (unsigned i = 0; i < 2; ++i) uniforms.scanHeading[i] = g_heading[i].load();
#if SURVEY_CONSTRAINED
    const float pulseRange = g_pulseRange.load();
#endif
    const auto seconds = pulseSeconds();
    const bool validAnchor = g_anchorValid.load();
    // An odd or changed sequence means beginPulse is mid-update; skip this frame.
    if ((sequence & 1) || sequence != g_pulseSequence.load()) return;
    if (!validAnchor || seconds >= kImprintSeconds) return;
    if (!scene || !hasTarget(drawContext, context)) { refuse(kDrawTarget); return; }
    auto* command = read<NVNcommandBuffer*>(drawContext, 0xb8);
    if (!command) { refuse(kCommandBuffer); return; }
    if (!initializeProgram()) return;

#if SURVEY_CONSTRAINED
    uniforms.settings[1] = pulseRange;
#endif
    uniforms.scanOrigin[3] = 1.0f;
    uniforms.scanHeading[2] = scanConeCosHalf();
    uniforms.scanStyle[1] = kFitCandidates;
    uniforms.scanStyle[3] = kScanOpacity;
    uniforms.scanMotion[1] = 0.22f;
    uniforms.scanMotion[2] = 6.f;
    uniforms.surfaceStyle[0] = imprintFront(seconds);
    uniforms.surfaceStyle[2] = kGridSpacing;
    uniforms.surfaceStyle[3] = kImprintHalfWidth;

    const auto* buffers = read<const void*>(scene, game.layout.sceneBuffers);
    if (!buffers || read<unsigned>(buffers, 8) < 1) { refuse(kDepthBuffers); return; }
    auto* record = read<unsigned char*>(buffers, 0x10);
    if (!record) { refuse(kDepthRecord); return; }
    const auto flags = read<unsigned>(record, 8);
    const auto slot = depthSlot(flags, game.layout.depthFull, game.layout.depthHalf);
    if (!slot) { refuse(kDepthSlot, flags); return; }
    void* sampler = record + slot;
    const auto width = read<std::uint16_t>(sampler, game.layout.textureWidth);
    const auto height = read<std::uint16_t>(sampler, game.layout.textureWidth + 2);
    if (!validDimensions(width, height)) { refuse(kDepthSize, width); return; }
    if (!cameraUniforms(context, uniforms)) { refuse(kCamera); return; }
    uniforms.settings[3] = 1.0f;
    uniforms.dimensions[0] = float(width); uniforms.dimensions[1] = float(height);

    const auto* allocator = global(game.variables.uniformAllocator);
    if (!allocator || !read<void*>(allocator, 24) || !read<unsigned>(allocator, 72)) {
        refuse(kUniformAllocator); return;
    }
    void* block = nullptr;
    native<void(*)(void**, const void*, std::size_t)>(game.calls.uniformBlock)(&block, &uniforms, sizeof(uniforms));
    if (!block || !read<std::uint64_t>(block, game.layout.uniformAddress)) { refuse(kUniformBlock); return; }

    alignas(8) unsigned char state[128]{};
    native<void(*)(void*)>(game.calls.contextCtor)(state);
    state[0] = 0; state[1] = 0;
    write<unsigned>(state, 4, 1);
    state[40] = NVN_BLEND_FUNC_SRC_ALPHA; state[42] = NVN_BLEND_FUNC_ONE;
    state[41] = NVN_BLEND_FUNC_ZERO; state[43] = NVN_BLEND_FUNC_ONE;
    state[44] = state[45] = NVN_BLEND_EQUATION_ADD;
    native<void(*)(void*, void*)>(game.calls.contextApply)(state, drawContext);
    native<void(*)(void*, void*)>(game.calls.bind)(context, drawContext);
    const bool sampled = native<bool(*)(void*, void*, unsigned, void*)>(game.calls.activateSampler)(
        g_samplerBindings, drawContext, 0, sampler);
    if (sampled) {
        g_bindProgram(command, &g_program, 63);
        g_bindUniform(command, NVN_SHADER_STAGE_FRAGMENT, 0,
                      read<std::uint64_t>(block, game.layout.uniformAddress),
                      read<unsigned>(block, game.layout.uniformSize));
        g_drawArrays(command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
        g_lastRefusal = 0;
    } else refuse(kSampler);

    native<void(*)(void*, void*)>(game.calls.unbind)(context, drawContext);
    native<void(*)(void*, void*)>(game.calls.barrier)(read<void*>(context, game.layout.contextTarget), drawContext);
    native<void(*)(void*)>(game.calls.contextCtor)(state);
    native<void(*)(void*, void*)>(game.calls.contextApply)(state, drawContext);
}

struct PfxHook {
    inline static totk::render::PfxCallback previous{};
    static std::uint64_t Callback(void* extension, void* args) {
        const auto result = previous(extension, args);
        if (!args || read<unsigned>(args, 0x1c) != 0 || read<unsigned>(args, 0x18) != 1) return result;
        auto* context = read<void*>(args, 0x10);
        if (!context || read<std::uint8_t>(context, 8) != 0) return result;
        const auto& layout = zonai_survey::profiles::active->layout;
        auto* drawContext = read<void*>(args, 0);
        // Camera tracking must run even with no active pulse and no visible labels.
        const auto* camera = read<const unsigned char*>(context, 0x18);
        if (camera) {
            const auto* view = reinterpret_cast<const float*>(camera + layout.cameraView);
            zonai_survey::engine::publishCameraForward(-view[8], -view[9], -view[10]);
        }
        draw(drawContext, read<void*>(args, 8), context);
        if (camera && hasTarget(drawContext, context) && zonai_survey::render::atlasHasContent() &&
            initializeGpuStorage()) {
            zonai_survey::atlas_gpu::draw(g_base, g_device, g_getProc, drawContext, context,
                reinterpret_cast<const float*>(camera + layout.cameraView),
                reinterpret_cast<const float*>(camera + layout.cameraProjection));
        }
        return result;
    }
};
}

void install(std::uintptr_t mainBase) {
    g_base = mainBase;
    const auto& site = zonai_survey::profiles::active->pfx;
    if (!totk::render::installPfxHook(mainBase,
                                     totk::render::PfxSite{static_cast<std::uintptr_t>(site.offset), site.first, site.second},
                                     PfxHook::Callback,
                                     PfxHook::previous, "survey")) return;
    Logging.Log("[survey-pulse] installed\n");
}

float pulseSeconds() {
    const auto start = g_startTick.load();
    return start ? float(svcGetSystemTick() - start) / zonai_survey::pure::kSystemTicksPerSecond : 0.0f;
}
float markerArrivalSeconds(float distance) { return imprintSecondsToReach(distance); }
bool pulseRunning() { return g_anchorValid.load() && pulseSeconds() < kImprintSeconds; }
bool beginPulse(float x, float y, float z, float hx, float hz) {
    const float length = std::sqrt(hx*hx + hz*hz);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(length) || length < 0.5f) {
        Logging.Log("[survey-pulse] pulse refused invalid origin/heading\n");
        return false;
    }
    ++g_pulseSequence;
    g_origin[0] = x; g_origin[1] = y; g_origin[2] = z;
    g_heading[0] = hx/length; g_heading[1] = hz/length;
#if SURVEY_CONSTRAINED
    g_pulseRange = zonai_survey::options::nextRange();
#endif
    g_startTick = svcGetSystemTick(); g_anchorValid = true;
    ++g_pulseSequence;
    return true;
}
void endPulse() { g_anchorValid = false; }
}
