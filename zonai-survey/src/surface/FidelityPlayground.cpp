// SPDX-License-Identifier: MIT
#include "FidelityPlayground.hpp"
#include "FidelityPolicy.hpp"
#include "FidelityTrace.hpp"
#include "FidelityShaders.hpp"
#include "../pure/SurveyOptions.hpp"
#include "CameraHeading.hpp"
#include "AtlasGpu.hpp"
#include "AtlasRenderer.hpp"
#include <lib.hpp>
#include <nvn/nvn.h>
#include <atomic>
#include <cstring>
#include "totk/engine/Npad.hpp"
#include "totk/render/PfxHook.hpp"

namespace survey_fidelity {
namespace {

constexpr std::uintptr_t kGetProcSlot = 0x46170f0;
constexpr std::uintptr_t kGraphicsSlot = 0x462ef88;
constexpr std::uintptr_t kUniformAllocatorSlot = 0x46382e0;
std::uintptr_t g_base{};
std::atomic<unsigned> g_mode{}, g_pulseSequence{};
std::atomic<unsigned> g_imprint{1};
std::atomic<float> g_origin[3]{}, g_heading[2]{};
#if SURVEY_CONSTRAINED || SURVEY_TUNING
std::atomic<float> g_pulseRange{zonai_survey::options::nextRange()};
#endif
std::atomic<std::uint64_t> g_startTick{};
std::atomic<bool> g_anchorValid{};
totk::engine::NpadReader g_input{};
std::uint64_t g_buttons{}, g_owned{};
std::uint64_t g_draws{}, g_callbacks{};
std::uint64_t g_lastRenderTick{};
float g_frameSeconds = 1.f/30.f;
unsigned g_lastRefusal{}, g_lastSource{}, g_lastWidth{}, g_lastHeight{}, g_pulseSeen{};
std::uint64_t g_memoryPeak{}, g_cpuTicks{}, g_cpuCalls{};
bool g_midpointProfiled{}, g_idleProfiled{};
bool g_ready{}, g_attempted{}, g_programReady[4]{}, g_programAttempted[4]{};
unsigned g_lastDrawMode{};
unsigned g_traceMode{}, g_traceFrames{};
NVNbufferAddress g_codeAddress{};
constexpr auto kFragmentOffset = (sizeof(shaders::vertCode) + 255) & ~std::size_t(255);
constexpr auto kCodeEnd=kFragmentOffset+sizeof(shaders::fragCode);
// The driver requires padding after the final shader program.
constexpr auto kCodePoolBytes=(kCodeEnd+16384+4095)&~std::size_t(4095);
static_assert(kCodeEnd<=49152 && kCodePoolBytes<=65536);
alignas(4096) unsigned char g_codeMemory[kCodePoolBytes]{};
alignas(8) NVNmemoryPool g_codePool{};
alignas(8) NVNprogram g_programs[4]{};
NVNdevice* g_device{};
PFNNVNDEVICEGETPROCADDRESSPROC g_getProc{};
PFNNVNCOMMANDBUFFERBINDPROGRAMPROC g_bindProgram{};
PFNNVNCOMMANDBUFFERDRAWARRAYSPROC g_drawArrays{};
PFNNVNCOMMANDBUFFERBINDUNIFORMBUFFERPROC g_bindUniform{};

alignas(8) unsigned char g_samplerBindings[128]{};
alignas(8) std::uint32_t g_samplerLocation[4]{0xffff00ff, 0, 0, 0};

void memoryProfile(const char* phase) {
    u64 total{}, used{}, systemTotal{}, systemUsed{};
    const auto a = svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    const auto b = svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    const auto c = svcGetInfo(&systemTotal, InfoType_SystemResourceSizeTotal, CUR_PROCESS_HANDLE, 0);
    const auto d = svcGetInfo(&systemUsed, InfoType_SystemResourceSizeUsed, CUR_PROCESS_HANDLE, 0);
    if (!b && used > g_memoryPeak) g_memoryPeak = used;
    Logging.Log("[survey-fidelity] MEMORY phase=%s total=%llu used=%llu peak=%llu system=%llu/%llu rc=%x,%x,%x,%x\n",
        phase, total, used, g_memoryPeak, systemUsed, systemTotal, a, b, c, d);
    trace::record(phase, g_mode.load(), total, used, systemUsed, systemTotal);
    trace::record("memory-query-results", g_mode.load(), a, b, c, d);
}

template<class T> T read(const void* p, std::size_t offset) {
    T value{}; std::memcpy(&value, static_cast<const unsigned char*>(p) + offset, sizeof(T)); return value;
}
template<class T> void write(void* p, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char*>(p) + offset, &value, sizeof(T));
}
template<class F> F native(std::uintptr_t offset) { return reinterpret_cast<F>(g_base + offset); }
void* global(std::uintptr_t offset) {
    const auto slot = read<std::uintptr_t>(reinterpret_cast<void*>(g_base), offset);
    const auto& module = exl::util::GetMainModuleInfo();
    if (slot < module.m_Text.m_Start || slot >= g_base + 0x6000000 || (slot & 7)) return nullptr;
    return read<void*>(reinterpret_cast<void*>(slot), 0);
}
void refuse(unsigned reason, unsigned detail = 0) {
    if (g_lastRefusal != reason) {
        g_lastRefusal = reason;
        Logging.Log("[survey-fidelity] REFUSED reason=%u detail=%u mode=%u callbacks=%llu\n",
                    reason, detail, g_mode.load(), static_cast<unsigned long long>(g_callbacks));
        trace::record("refused", g_mode.load(), reason, detail, g_callbacks);
    }
}
template<class F> bool resolve(F& target, const char* name) {
    target = reinterpret_cast<F>(g_getProc(g_device, name));
    if (!target) Logging.Log("[survey-fidelity] missing NVN function %s\n", name);
    return target != nullptr;
}

bool initializeGpuStorage() {
    if (g_ready) return true;
    if (g_attempted) return false;
    auto* graphics = global(kGraphicsSlot);
    g_getProc = reinterpret_cast<PFNNVNDEVICEGETPROCADDRESSPROC>(global(kGetProcSlot));
    if (!graphics || !g_getProc) { refuse(1); return false; }
    g_device = read<NVNdevice*>(graphics, 0x30);
    if (!g_device) { refuse(2); return false; }
    g_attempted = true;
    trace::record("gpu-resolve", g_mode.load());
    Logging.Log("[survey-fidelity] GPU_INIT resolving driver entry points\n");
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
        !resolve(g_bindUniform, "nvnCommandBufferBindUniformBuffer")) { refuse(3); return false; }
    int padding = -1;
    getInteger(g_device, NVN_DEVICE_INFO_SHADER_CODE_MEMORY_POOL_PADDING_SIZE, &padding);
    if (padding < 0 || kCodeEnd + padding > sizeof(g_codeMemory)) {
        refuse(4, unsigned(padding)); return false;
    }
    std::memcpy(g_codeMemory, shaders::vertCode, sizeof(shaders::vertCode));
    std::memcpy(g_codeMemory + kFragmentOffset, shaders::fragCode, sizeof(shaders::fragCode));
    alignas(8) NVNmemoryPoolBuilder builder{};
    builderDefaults(&builder);
    builderDevice(&builder, g_device);
    builderStorage(&builder, g_codeMemory, sizeof(g_codeMemory));
    builderFlags(&builder, NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED |
                           NVN_MEMORY_POOL_FLAGS_SHADER_CODE);
    Logging.Log("[survey-fidelity] GPU_INIT creating shader pool padding=%d\n", padding);
    trace::record("pool-initialize", g_mode.load(), padding);
    if (!poolInitialize(&g_codePool, &builder)) { refuse(5); return false; }
    poolFlush(&g_codePool, 0, sizeof(g_codeMemory));
    g_codeAddress = poolAddress(&g_codePool);
    if (!g_codeAddress) { refuse(6); return false; }
    write<std::uint32_t>(g_samplerBindings, 0x68, 1);
    write<const void*>(g_samplerBindings, 0x70, g_samplerLocation);
    g_ready = true;
    trace::record("pool-ready", g_mode.load(), g_codeAddress);
    memoryProfile("gpu-storage-ready");
    Logging.Log("[survey-fidelity] GPU_STORAGE build=surface14 compiler=uam-nvn pool=%u padding=%d vertex=%u fragment=%u\n",
                unsigned(sizeof(g_codeMemory)), padding, unsigned(sizeof(shaders::vertCode)),
                unsigned(sizeof(shaders::fragCode)));
    return true;
}

bool initializeGpu(unsigned index) {
    if (g_programReady[index]) return true;
    if (g_programAttempted[index] || !initializeGpuStorage()) return false;
    g_programAttempted[index] = true;
    PFNNVNPROGRAMINITIALIZEPROC programInitialize{};
    PFNNVNPROGRAMSETSHADERSPROC programSetShaders{};
    if (!resolve(programInitialize, "nvnProgramInitialize") ||
        !resolve(programSetShaders, "nvnProgramSetShaders")) { refuse(3); return false; }
    if (index!=3) { refuse(18,index); return false; }
    const unsigned char* controls[]{nullptr,nullptr,nullptr,shaders::fragControl};
    const std::size_t offsets[]{0,0,0,kFragmentOffset};
    trace::record("program-initialize", g_mode.load(), index);
    Logging.Log("[survey-fidelity] GPU_PROGRAM initializing index=%u\n", index);
    if (!programInitialize(&g_programs[index], g_device)) { refuse(6); return false; }
    const NVNshaderData stages[]{{g_codeAddress, shaders::vertControl},
        {g_codeAddress + offsets[index], controls[index]}};
    trace::record("program-set-shaders", g_mode.load(), index, offsets[index]);
    Logging.Log("[survey-fidelity] GPU_PROGRAM installing index=%u\n", index);
    if (!programSetShaders(&g_programs[index], 2, stages)) { refuse(7); return false; }
    g_programReady[index] = true;
    trace::record("program-ready", g_mode.load(), index);
    Logging.Log("[survey-fidelity] GPU_READY build=surface14 index=%u\n", index);
    return true;
}

bool cameraUniforms(void* context, Uniforms& uniforms) {
    const auto* camera = read<const unsigned char*>(context, 0x18);
    if (!camera) return false;
    float projection[16], view[16]{}, inverseView[16];
    std::memcpy(projection, camera + 0x160, sizeof(projection));
    std::memcpy(view, camera + 0x100, 12 * sizeof(float));
    view[15] = 1;
    if (!inverse4(projection, uniforms.inverseProjection) || !inverse4(view, inverseView)) return false;
    std::memcpy(uniforms.inverseView, inverseView, sizeof(uniforms.inverseView));

    uniforms.dimensions[2] = read<float>(camera, 0x1e0);
    uniforms.dimensions[3] = read<float>(camera, 0x1e4);
    const auto perspective = read<std::uint8_t>(camera, 0x280);
    uniforms.settings[2] = float(perspective);
    if (perspective > 1 || !validClipRange(uniforms.dimensions[2], uniforms.dimensions[3])) {
        if (g_lastRefusal != 13) Logging.Log("[survey-fidelity] CAMERA_REFUSED near=%.5f far=%.2f perspective=%u\n",
                    uniforms.dimensions[2], uniforms.dimensions[3], perspective);
        return false;
    }
    return true;
}

void draw(void* drawContext, void* scene, void* context) {
    const auto renderTick = svcGetSystemTick();
    if (g_lastRenderTick) {
        const float dt = float(renderTick-g_lastRenderTick)/19200000.f;
        if (dt >= 1.f/240.f && dt <= 0.1f)
            g_frameSeconds += 0.15f*(dt-g_frameSeconds);
    }
    g_lastRenderTick = renderTick;
    constexpr unsigned selectedMode=0;
    const auto sequence = g_pulseSequence.load();
    Uniforms uniforms{};
    for (unsigned i = 0; i < 3; ++i) uniforms.scanOrigin[i] = g_origin[i].load();
    for (unsigned i = 0; i < 2; ++i) uniforms.scanHeading[i] = g_heading[i].load();
#if SURVEY_CONSTRAINED || SURVEY_TUNING
    const float pulseRange = g_pulseRange.load();
#endif
    const auto seconds = pulseSeconds();
    const bool validAnchor = g_anchorValid.load();
    if ((sequence & 1) || sequence != g_pulseSequence.load()) return;
    const bool imprint = imprintEnabled();
    const bool running = validAnchor && seconds < (imprint ? kImprintSeconds : kScanSeconds);
    const unsigned mode = selectedMode ? selectedMode : running ? unsigned(Mode::Stripe) : 0;
    if (!mode) {
        if (g_traceMode) { trace::record("off", 0); g_traceMode = 0; }
        if (g_pulseSeen && !g_idleProfiled) {
            memoryProfile("scan-idle"); g_idleProfiled = true;
            Logging.Log("[survey-fidelity] COST draws=%llu cpu_calls=%llu cpu_ticks=%llu geometry=0 owned_depth=0 uniform_bytes_per_draw=256\n",
                g_draws, g_cpuCalls, g_cpuTicks);
        }
        return;
    }
    const bool firstEntry = mode != g_traceMode || sequence != g_pulseSeen;
    if (sequence != g_pulseSeen) {
        g_pulseSeen = sequence; g_midpointProfiled = false; g_idleProfiled = false;
        memoryProfile("scan-first-frame");
    }
    if (running && seconds >= (imprint ? kImprintSeconds : kScanSeconds) * 0.5f && !g_midpointProfiled) {
        memoryProfile("scan-midpoint"); g_midpointProfiled = true;
    }
    if (firstEntry) {
        trace::record("mode-entry", mode, reinterpret_cast<std::uintptr_t>(drawContext),
                      reinterpret_cast<std::uintptr_t>(scene), reinterpret_cast<std::uintptr_t>(context));
        g_traceMode = mode; g_traceFrames = 0;
    }
    if (!drawContext || !scene || !context || !read<void*>(context, 0x540) ||
        !read<void*>(context, 0x548)) { refuse(8); return; }
    auto* command = read<NVNcommandBuffer*>(drawContext, 0xb8);
    if (!command) { refuse(17); return; }
    const auto kind = Mode(mode);
    const bool depth = usesDepth(kind), uniform = usesUniforms(kind);
    const auto index = programIndex(kind);
    if (!initializeGpu(index)) return;

    uniforms.settings[0] = kind == Mode::Stripe ? 3.0f : 2.0f;
    uniforms.settings[1] = 0;
#if SURVEY_CONSTRAINED || SURVEY_TUNING
    uniforms.settings[1] = pulseRange;
#endif
    uniforms.scanOrigin[3] = validAnchor ? 1.0f : 0.0f;
    uniforms.scanHeading[2] = scanConeCosHalf();
    uniforms.scanHeading[3] = selectedMode == unsigned(Mode::Stripe) ? kBandCount*kBandSpacing : pulseFront(seconds);
    uniforms.scanStyle[0] = kBandSpacing; uniforms.scanStyle[1] = kBandCount;
    uniforms.scanStyle[2] = kBandHalfWidth; uniforms.scanStyle[3] = kBandOpacity;
    uniforms.scanMotion[0] = selectedMode == unsigned(Mode::Stripe) ? 0.f : trailLength(g_frameSeconds);
    uniforms.scanMotion[1] = 0.22f;
    uniforms.scanMotion[2] = 6.f;
    uniforms.scanMotion[3] = kContourRise;
    uniforms.surfaceStyle[0] = selectedMode == unsigned(Mode::Stripe) ? -1.f : imprintFront(seconds);
    uniforms.surfaceStyle[1] = imprint ? 1.f : 0.f;
    uniforms.surfaceStyle[2] = kGridSpacing;
    uniforms.surfaceStyle[3] = kImprintHalfWidth;
    void* sampler = nullptr;
    bool cameraFailure = false;
    if (kind == Mode::UniformCheck) {
        uniforms.inverseProjection[0] = 0.1f; uniforms.inverseProjection[1] = 0.9f;
        uniforms.inverseProjection[2] = 0.2f; uniforms.inverseProjection[3] = 1.0f;
    }
    if (depth) {
        if (firstEntry) trace::record("depth-select", mode);
        const auto* buffers = read<const void*>(scene, 0x1ec0);
        if (!buffers || read<unsigned>(buffers, 8) < 1) { refuse(9); return; }
        auto* record = read<unsigned char*>(buffers, 0x10);
        if (!record) { refuse(10); return; }
        const auto flags = read<unsigned>(record, 8);
        const auto slot = depthSlot(flags);
        if (!slot) { refuse(11, flags); return; }
        sampler = record + slot;
        const auto width = read<std::uint16_t>(sampler, 0x30);
        const auto height = read<std::uint16_t>(sampler, 0x32);
        if (!validDimensions(width, height)) { refuse(12, width); return; }
        if (firstEntry) {
            trace::record("depth-source", mode, slot, flags, width, height);
            trace::record("sampler-ids", mode, read<unsigned>(sampler, 0x58),
                read<std::uint16_t>(sampler, 0xea), read<unsigned>(sampler, 0xe4));
            trace::record("sampler-view-words", mode, read<std::uint64_t>(sampler, 0),
                read<std::uint64_t>(sampler, 8), read<std::uint64_t>(sampler, 16),
                read<std::uint64_t>(sampler, 0xa8));
        }
        if (firstEntry && uniform) trace::record("camera-read", mode);
        const bool cameraValid = !uniform || cameraUniforms(context, uniforms);
        if (firstEntry && uniform) trace::record("camera-ready", mode, cameraValid);
        if (firstEntry && uniform && cameraValid)
            Logging.Log("[survey-fidelity] CAMERA NLD_STORED near=%.5f far=%.2f position=%.2f,%.2f,%.2f origin=%.2f,%.2f,%.2f ndc_flip=%u\n",
                uniforms.dimensions[2], uniforms.dimensions[3], uniforms.inverseView[3], uniforms.inverseView[7],
                uniforms.inverseView[11], uniforms.scanOrigin[0], uniforms.scanOrigin[1], uniforms.scanOrigin[2], 0u);
        uniforms.settings[3] = cameraValid ? 1.0f : 0.0f;
        if (!cameraValid) {
            cameraFailure = true;
            refuse(13);
            if (mode == unsigned(Mode::Stripe)) return;
        }
        uniforms.dimensions[0] = float(width); uniforms.dimensions[1] = float(height);
        if (slot != g_lastSource || width != g_lastWidth || height != g_lastHeight) {
            Logging.Log("[survey-fidelity] DEPTH_SOURCE slot=%x flags=%x size=%ux%u view=0\n",
                        slot, flags, width, height);
            g_lastSource = slot; g_lastWidth = width; g_lastHeight = height;
        }
    }
    void* block = nullptr;
    if (uniform) {
        if (firstEntry) trace::record("uniform-allocate", mode);
        const auto* allocator = global(kUniformAllocatorSlot);
        if (!allocator || !read<void*>(allocator, 24) || !read<unsigned>(allocator, 72)) {
            refuse(14); return;
        }
        native<void(*)(void**, const void*, std::size_t)>(0x95f604)(&block, &uniforms, sizeof(uniforms));
        if (!block || !read<std::uint64_t>(block, 120)) { refuse(15); return; }
        if (firstEntry) trace::record("uniform-ready", mode, reinterpret_cast<std::uintptr_t>(block),
            read<std::uint64_t>(block, 120), read<unsigned>(block, 56));
    }
    const bool firstDraw = mode != g_lastDrawMode;
    if (firstEntry) trace::record("framebuffer-bind", mode);
    if (firstDraw) Logging.Log("[survey-fidelity] DRAW_BEGIN mode=%u index=%u\n", mode, index);
    alignas(8) unsigned char state[128]{};
    native<void(*)(void*)>(0x74c19c)(state);
    state[0] = 0; state[1] = 0;
    if (kind == Mode::Stripe) {

        write<unsigned>(state, 4, 1);
        state[40] = NVN_BLEND_FUNC_SRC_ALPHA; state[42] = NVN_BLEND_FUNC_ONE;
        state[41] = NVN_BLEND_FUNC_ZERO; state[43] = NVN_BLEND_FUNC_ONE;
        state[44] = state[45] = NVN_BLEND_EQUATION_ADD;
    }
    native<void(*)(void*, void*)>(0x756a08)(state, drawContext);
    native<void(*)(void*, void*)>(0xc5a6fc)(context, drawContext);
    if (firstEntry && depth) trace::record("sampler-bind", mode);
    const bool sampled = !depth || (sampler && native<bool(*)(void*, void*, unsigned, void*)>(0x95ef6c)(
                                           g_samplerBindings, drawContext, 0, sampler));
    if (firstEntry && depth) trace::record("sampler-bound", mode, sampled,
        read<unsigned>(sampler, 0x58), read<std::uint16_t>(sampler, 0xea), read<unsigned>(sampler, 0xe4));
    if (sampled) {
        if (firstDraw) Logging.Log("[survey-fidelity] DRAW_BIND mode=%u\n", mode);
        if (firstEntry) trace::record("program-bind", mode, index);
        g_bindProgram(command, &g_programs[index], 63);

        if (firstEntry && uniform) trace::record("uniform-bind", mode);
        if (uniform)
            g_bindUniform(command, NVN_SHADER_STAGE_FRAGMENT, 0, read<std::uint64_t>(block, 120),
                          read<unsigned>(block, 56));
        if (firstDraw) Logging.Log("[survey-fidelity] DRAW_SUBMIT mode=%u\n", mode);
        if (firstEntry) trace::record("draw-submit", mode);
        g_drawArrays(command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
        if (firstEntry) trace::record("draw-recorded", mode);
        if (firstDraw || (g_lastRefusal && !cameraFailure) || (g_draws % 300) == 0)
            Logging.Log("[survey-fidelity] DRAW mode=%u NLD=1 ndc_flip=%u draws=%llu source=%x\n",
                        mode, 0u,
                        static_cast<unsigned long long>(g_draws), g_lastSource);
        ++g_draws;
        g_lastDrawMode = mode;
        if (!cameraFailure) g_lastRefusal = 0;
    } else refuse(16);

    native<void(*)(void*, void*)>(0x962c18)(context, drawContext);
    native<void(*)(void*, void*)>(0xc4b7ac)(read<void*>(context, 0x540), drawContext);
    native<void(*)(void*)>(0x74c19c)(state);
    native<void(*)(void*, void*)>(0x756a08)(state, drawContext);
    ++g_traceFrames;
    if (firstEntry || g_traceFrames == 10 || g_traceFrames == 60)
        trace::record("callback-complete-not-gpu-fence", mode, g_traceFrames, sampled);
}

struct PfxHook {
    inline static totk::render::PfxCallback previous{};
    static std::uint64_t Callback(void* extension, void* args) {
        const auto result = previous(extension, args);
        if (!args || read<unsigned>(args, 0x1c) != 0 || read<unsigned>(args, 0x18) != 1) return result;
        auto* context = read<void*>(args, 0x10);
        if (!context || read<std::uint8_t>(context, 8) != 0) return result;
        ++g_callbacks;
        const auto start = svcGetSystemTick();
        // Camera tracking must run even with no active pulse and no visible labels.
        const auto* camera=read<const unsigned char*>(context,0x18);
        if(camera) {
            const auto* view=reinterpret_cast<const float*>(camera+0x100);
            zonai_survey::engine::publishCameraForward(-view[8],-view[9],-view[10]);
        }
        draw(read<void*>(args, 0), read<void*>(args, 8), context);
        auto* drawContext=read<void*>(args,0);
        if(camera && drawContext && read<void*>(context,0x540) && read<void*>(context,0x548) &&
           zonai_survey::render::atlasHasContent() && initializeGpuStorage()) {
            zonai_survey::atlas_gpu::draw(g_base,g_device,g_getProc,drawContext,context,
                reinterpret_cast<const float*>(camera+0x100),reinterpret_cast<const float*>(camera+0x160));
        }
        g_cpuTicks += svcGetSystemTick() - start; ++g_cpuCalls;
        return result;
    }
};
}

void install(std::uintptr_t mainBase) {
    g_base = mainBase;
    if (!totk::render::installPfxHook(mainBase, PfxHook::Callback,
                                     PfxHook::previous, "survey")) return;
    memoryProfile("install");
    Logging.Log("[survey-fidelity] surface14 installed; diagnostics=%u cone=100 imprint_seconds=3.2 opacity=1.0; ring=%u\n", unsigned(SURVEY_FIDELITY_PLAYGROUND), kTextOnlyRingBytes);
}

float pulseSeconds() {
    const auto start = g_startTick.load();
    return start ? float(svcGetSystemTick() - start) / 19200000.0f : 0.0f;
}
bool imprintEnabled() {
    return true;
}
float markerArrivalSeconds(float distance) {
    return imprintEnabled() ? imprintSecondsToReach(distance) : secondsToReach(distance);
}
bool pulseRunning() { return g_anchorValid.load() && pulseSeconds() < (imprintEnabled() ? kImprintSeconds : kScanSeconds); }
bool beginPulse(float x, float y, float z, float hx, float hz) {
    const float length = std::sqrt(hx*hx + hz*hz);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(length) || length < 0.5f) {
        Logging.Log("[survey-fidelity] pulse refused invalid origin/heading\n");
        return false;
    }
    trace::begin();
    ++g_pulseSequence;
    g_origin[0] = x; g_origin[1] = y; g_origin[2] = z;
    g_heading[0] = hx/length; g_heading[1] = hz/length;
#if SURVEY_CONSTRAINED || SURVEY_TUNING
    g_pulseRange = zonai_survey::options::nextRange();
#endif
    g_startTick = svcGetSystemTick(); g_anchorValid = true; g_mode = 0;
    ++g_pulseSequence;
    Logging.Log("[survey-fidelity] PULSE origin=%.2f,%.2f,%.2f heading=%.3f,%.3f bands=%u spacing=%.1f seconds=%.1f\n",
                x,y,z,hx/length,hz/length,kBandCount,kBandSpacing,imprintEnabled() ? kImprintSeconds : kScanSeconds);
    return true;
}
void endPulse() { g_anchorValid = false; }

void tick(void* device) {
    (void)device;
}
}
