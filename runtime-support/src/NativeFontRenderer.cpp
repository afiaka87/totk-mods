// SPDX-License-Identifier: MIT
// Copyright (C) Clay Mullis

// Native UI target, NVN pools and fenced upload slots transfer from accepted BindFS rendering.
#include "totk/ui/NativeFontRenderer.hpp"
#include "native-font/NvnAbi.hpp"
#include "native-font/NativeFontShaders.hpp"
#include "native-font/Offsets.hpp"

namespace totk::ui::native_font {
namespace {
namespace n = nvn;
constexpr std::size_t aligned(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}
constexpr auto kFragment = aligned(sizeof(shaders::vertCode), 256);
constexpr auto kCodeEnd = kFragment + sizeof(shaders::fragCode);
constexpr auto kSlotBegin = aligned(kCodeEnd + 16384, 4096);
constexpr auto kSlotBytes = aligned(kQuads * sizeof(Quad), 4096);
alignas(4096) unsigned char storage[kSlotBegin + 2 * kSlotBytes]{};
n::Pool pool{}, fontPool{};
n::Program program{};
n::Sync syncs[2]{};
n::Address address{}, fontAddress{};
std::uintptr_t mainBase = 0;
Atlas atlas{};
Services services{};
Mailbox<Frame> frames{};
UiCallback previousUi = nullptr;
bool configured = false, attempted = false, ready = false, pending[2]{};
unsigned nextSlot = 0;
n::Flush flush{};
n::Bind bindProgram{};
n::Uniform uniform{};
n::Draw drawArrays{};
n::Fence fence{};
n::Barrier barrier{};
n::Wait wait{};

void report(const char* message) {
    if (services.diagnostic) services.diagnostic(services.context, message);
}
template <class T> T read(std::uintptr_t at) {
    T value{};
    if (services.read && services.read(services.context, at, &value, sizeof(value))) return value;
    return {};
}
template <class F> F native(std::uintptr_t offset) {
    return reinterpret_cast<F>(services.resolve ? services.resolve(services.context, offset) :
                              reinterpret_cast<void*>(mainBase + offset));
}
template <class F> bool load(F& function, n::Device* device, n::GetProc get, const char* name) {
    function = reinterpret_cast<F>(get(device, name));
    if (function) return true;
    report(name);
    return false;
}
bool initializeGpu(n::Device* device, n::GetProc get) {
    if (ready) return true;
    if (attempted) return false;
    attempted = true;
    void (*defaults)(n::Builder*){};
    void (*setDevice)(n::Builder*, n::Device*){};
    void (*setStorage)(n::Builder*, void*, std::size_t){};
    void (*setFlags)(n::Builder*, int){};
    n::Bool (*initPool)(n::Pool*, const n::Builder*){};
    n::Address (*getAddress)(n::Pool*){};
    void (*integer)(n::Device*, int, int*){};
    n::Bool (*initProgram)(n::Program*, n::Device*){};
    n::Bool (*setShaders)(n::Program*, int, const n::Shader*){};
    n::Bool (*initSync)(n::Sync*, n::Device*){};
#define LOAD(function, name) if (!load(function, device, get, name)) return false;
    LOAD(defaults, "nvnMemoryPoolBuilderSetDefaults")
    LOAD(setDevice, "nvnMemoryPoolBuilderSetDevice")
    LOAD(setStorage, "nvnMemoryPoolBuilderSetStorage")
    LOAD(setFlags, "nvnMemoryPoolBuilderSetFlags")
    LOAD(initPool, "nvnMemoryPoolInitialize")
    LOAD(getAddress, "nvnMemoryPoolGetBufferAddress")
    LOAD(integer, "nvnDeviceGetInteger")
    LOAD(initProgram, "nvnProgramInitialize")
    LOAD(setShaders, "nvnProgramSetShaders")
    LOAD(initSync, "nvnSyncInitialize")
    LOAD(flush, "nvnMemoryPoolFlushMappedRange")
    LOAD(bindProgram, "nvnCommandBufferBindProgram")
    LOAD(uniform, "nvnCommandBufferBindUniformBuffer")
    LOAD(drawArrays, "nvnCommandBufferDrawArrays")
    LOAD(fence, "nvnCommandBufferFenceSync")
    LOAD(barrier, "nvnCommandBufferBarrier")
    LOAD(wait, "nvnSyncWait")
#undef LOAD
    int padding = -1, limit = 0;
    integer(device, n::kShaderPadding, &padding);
    integer(device, n::kUniformLimit, &limit);
    if (padding < 0 || kCodeEnd + static_cast<unsigned>(padding) > kSlotBegin || limit < int(kBankBytes)) {
        report("[native-font] GPU shader padding/uniform budget refused");
        return false;
    }
    std::memcpy(storage, shaders::vertCode, sizeof(shaders::vertCode));
    std::memcpy(storage + kFragment, shaders::fragCode, sizeof(shaders::fragCode));
    n::Builder builder{};
    defaults(&builder);
    setDevice(&builder, device);
    setStorage(&builder, storage, sizeof(storage));
    setFlags(&builder, n::kCpuCached | n::kGpuCached | n::kShaderCode);
    if (!initPool(&pool, &builder) || !(address = getAddress(&pool))) {
        report("[native-font] shader/geometry pool refused");
        return false;
    }
    flush(&pool, 0, kSlotBegin);
    if (!initProgram(&program, device)) { report("[native-font] shader program refused"); return false; }
    const n::Shader stages[]{{address, shaders::vertControl}, {address + kFragment, shaders::fragControl}};
    if (!setShaders(&program, 2, stages)) { report("[native-font] shader stages refused"); return false; }
    setStorage(&builder, atlas.pixels, atlas.bytes);
    setFlags(&builder, n::kCpuCached | n::kGpuCached);
    if (!initPool(&fontPool, &builder) || !(fontAddress = getAddress(&fontPool))) {
        report("[native-font] private atlas pool refused");
        return false;
    }
    flush(&fontPool, 0, atlas.bytes);
    for (auto& sync : syncs)
        if (!initSync(&sync, device)) { report("[native-font] upload fence refused"); return false; }
    ready = true;
    report("[native-font] shader renderer ready");
    return true;
}

void draw(const Frame& frame, std::uintptr_t drawContext, std::uintptr_t target,
          std::uintptr_t viewport, std::uintptr_t restoreState) {
    if (!frame.count || !target || !viewport) return;
    const auto graphics = read<std::uintptr_t>(mainBase + offsets::graphics);
    auto* device = reinterpret_cast<n::Device*>(read<std::uintptr_t>(graphics + 0x30));
    const auto get = reinterpret_cast<n::GetProc>(read<std::uintptr_t>(mainBase + offsets::getProc));
    auto* command = reinterpret_cast<n::Command*>(read<std::uintptr_t>(drawContext + 0xb8));
    if (!device || !get || !command || !initializeGpu(device, get)) return;
    unsigned slot = 2;
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned candidate = (nextSlot + i) & 1;
        if (pending[candidate]) {
            const auto result = wait(&syncs[candidate], 0);
            if (result == n::kFailed) { report("[native-font] upload fence wait failed"); return; }
            if (result == n::kTimeout) continue;
            pending[candidate] = false;
        }
        slot = candidate;
        break;
    }
    if (slot == 2) return; // Both previous uploads remain in flight; never overwrite them.
    PixelGrid grid{};
    native<void (*)(void*, PixelGrid*, void*)>(offsets::viewportSize)(reinterpret_cast<void*>(viewport), &grid, reinterpret_cast<void*>(target));
    if (!grid.valid()) return;
    const auto complete = read<std::uintptr_t>(read<std::uintptr_t>(target) + 0x28);
    if (!complete) return;
    const auto offset = kSlotBegin + slot * kSlotBytes;
    auto* quads = reinterpret_cast<Quad*>(storage + offset);
    const auto count = buildQuads(frame, atlas, grid, quads, kQuads);
    if (!count) return;
    flush(&pool, offset, aligned(count * sizeof(Quad), 256));
    alignas(8) unsigned char state[128]{};
    const auto defaults = native<void (*)(void*)>(offsets::contextCtor);
    const auto apply = native<void (*)(void*, void*)>(offsets::contextApply);
    auto* context = reinterpret_cast<void*>(drawContext);
    auto* buffer = reinterpret_cast<void*>(target);
    defaults(state);
    state[0] = state[1] = state[101] = 0;
    const unsigned blend = 1;
    std::memcpy(state + 4, &blend, 4);
    state[40] = n::kSourceAlpha; state[42] = n::kInverseSourceAlpha;
    state[41] = n::kZero; state[43] = n::kOne; state[44] = state[45] = n::kAdd;
    apply(state, context);
    native<void (*)(void*, void*, void*)>(offsets::viewportApply)(reinterpret_cast<void*>(viewport), context, buffer);
    alignas(8) unsigned char bindScratch[40]{};
    native<void (*)(void*, void*, void*, unsigned)>(offsets::renderBufferBind)(buffer, context, bindScratch, 0);
    barrier(command, n::kInvalidateShader);
    bindProgram(command, &program, 63);
    for (unsigned first = 0; first < count;) {
        const auto run = nextRun(quads, count, first);
        if (!run.count) break;
        uniform(command, n::kFragment, 0, fontAddress + run.bank * kBankBytes, kBankBytes);
        uniform(command, n::kVertex, 0, address + offset + run.block * sizeof(Quad), kBatch * sizeof(Quad));
        drawArrays(command, n::kTriangles, static_cast<int>(run.firstVertex), static_cast<int>(run.count * 6));
        first += run.count;
    }
    fence(command, &syncs[slot], n::kAllComplete, 0);
    pending[slot] = true;
    nextSlot = (slot + 1) & 1;
    reinterpret_cast<void (*)(void*, void*)>(complete)(buffer, context);
    native<void (*)(void*, void*)>(offsets::barrier)(buffer, context);
    apply(reinterpret_cast<void*>(restoreState), context);
}

std::uint64_t uiCallback(void* module, void* args) {
    const auto result = previousUi(module, args);
    drawUi(reinterpret_cast<std::uintptr_t>(module), reinterpret_cast<std::uintptr_t>(args));
    return result;
}
} // namespace

bool configure(std::uintptr_t base, Atlas input, Services port) noexcept {
    if (configured) { report("[native-font] duplicate configure refused"); return false; }
    if (!base || !port.read || !validAtlas(input)) {
        if (port.diagnostic) port.diagnostic(port.context, "[native-font] configuration/atlas refused");
        return false;
    }
    services = port;
    const struct { std::uintptr_t offset; std::uint32_t word; } signatures[]{
        {offsets::uiDraw, 0xd10283ff}, {offsets::uiDraw + 4, 0xa9067bfd},
        {offsets::contextCtor, 0x52802028}, {offsets::contextApply, 0xd10243ff},
        {offsets::barrier, 0xa9be7bfd}, {offsets::viewportApply, 0xa9bc7bfd},
        {offsets::viewportSize, 0x2d420800}, {offsets::renderBufferBind, 0xd103c3ff}};
    for (const auto& signature : signatures)
        if (read<std::uint32_t>(base + signature.offset) != signature.word) {
            report("[native-font] TotK 1.2.1 native signature refused");
            return false;
        }
    mainBase = base;
    atlas = input;
    configured = true;
    return true;
}
bool installUiHook(HookInstaller installer, void* context) noexcept {
    if (previousUi) return true;
    UiCallback predecessor = nullptr;
    if (!configured || !installer || !installer(context, mainBase + offsets::uiDraw, uiCallback, predecessor) || !predecessor) {
        report("[native-font] native UI hook installation refused");
        return false;
    }
    previousUi = predecessor;
    return true;
}
bool publish(const Frame& frame) noexcept {
    if (!configured || !validFrame(frame)) { report("[native-font] frame refused"); return false; }
    frames.publish(frame);
    return true;
}
void clear() noexcept { frames.publish({}); }
void drawUi(std::uintptr_t module, std::uintptr_t args) noexcept {
    if (!configured || !module || !args) return;
    const auto info = read<std::uintptr_t>(args);
    const auto context = read<std::uintptr_t>(info + 0x58);
    const auto secondary = read<std::uintptr_t>(info + 0x20);
    const auto target = secondary ? secondary : read<std::uintptr_t>(info + 0x18);
    const auto viewport = read<std::uintptr_t>(info + 0x48);
    if (info && context && target && viewport) draw(frames.consume(), context, target, viewport, module + 0x110);
}
} // namespace totk::ui::native_font
