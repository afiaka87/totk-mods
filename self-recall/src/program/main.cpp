#include <lib.hpp>

#include <atomic>

#include "RecallRuntimeEngine.hpp"
#include "RecallModelEngine.hpp"
#include "RecallGraphicsEngine.hpp"
#include "RecallBase.hpp"
#include "StartupTrace.hpp"
#include "modules/self-recall/SelfRecallModule.hpp"

namespace {
std::atomic_flag g_firstRaycastEnter = ATOMIC_FLAG_INIT;
std::atomic_flag g_firstRaycastOriginalReturn = ATOMIC_FLAG_INIT;
std::atomic_flag g_firstRaycastModuleReturn = ATOMIC_FLAG_INIT;
std::atomic_flag g_firstNpadEnter = ATOMIC_FLAG_INIT;
std::atomic_flag g_firstNpadOriginalReturn = ATOMIC_FLAG_INIT;
std::atomic<std::uint64_t> g_npadReturnsAfterTrace{};
std::uint64_t g_lastClothingReport = 0;

void preparePoseStorage(std::uint64_t epoch) {
    self_recall::pose_recorder::beginFrame(epoch);
    self_recall::pose_render::beginFrame(epoch);
    self_recall::pose_storage::prepare();
    const auto generation = self_recall::pose_render::latchedGeneration(epoch);
    self_recall::palette::beginFrame(epoch, generation);
    self_recall::native_path::beginFrame(epoch, generation);
}

constexpr ptrdiff_t kNpadCalc = 0x02A267BC;
constexpr ptrdiff_t kRayCastWorker = 0x00858590;
HOOK_DEFINE_TRAMPOLINE(RayCastWorkerHook) {
    static u64 OriginalThunk(const void* from, const void* to,
                             const void* object, const void* out,
                             u32 mask, u32 flag) {
        return Orig(from, to, object, out, mask, flag);
    }

    static u64 Callback(const void* from, const void* to,
                        const void* object, const void* out,
                        u32 mask, u32 flag) {
        if (self_recall::startup_trace::ready() &&
            !g_firstRaycastEnter.test_and_set(std::memory_order_relaxed))
            self_recall::startup_trace::mark("30 raycast-enter", reinterpret_cast<std::uintptr_t>(object), mask);
        const u64 result =
            Orig(from, to, object, out, mask, flag);
        if (self_recall::startup_trace::ready() &&
            !g_firstRaycastOriginalReturn.test_and_set(std::memory_order_relaxed))
            self_recall::startup_trace::mark("31 raycast-original-return", result, flag);
        const auto& module = wwpg::modules::selfRecall();
        if (module.onRaycast) {
            module.onRaycast(&OriginalThunk, from, to, object,
                             out, mask, flag);
        }
        if (self_recall::startup_trace::ready() &&
            !g_firstRaycastModuleReturn.test_and_set(std::memory_order_relaxed))
            self_recall::startup_trace::mark("32 raycast-module-return", result, flag);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(NpadCalcHook) {
    static void Callback(void* device) {
        if (self_recall::startup_trace::ready() &&
            !g_firstNpadEnter.test_and_set(std::memory_order_relaxed))
            self_recall::startup_trace::mark("40 npad-enter", reinterpret_cast<std::uintptr_t>(device), 0);
        Orig(device);
        if (self_recall::startup_trace::ready() &&
            !g_firstNpadOriginalReturn.test_and_set(std::memory_order_relaxed))
            self_recall::startup_trace::mark("41 npad-original-return", reinterpret_cast<std::uintptr_t>(device), 0);
        wwpg::modules::selfRecall().tick(device);

        // The repository's proven SD-card rule is to mount only after the player
        // has resolved; filesystem services are not ready in exl_main.
        if (!self_recall::startup_trace::ready() && self_recall::world::havePlayer())
            self_recall::startup_trace::begin();
        if (!self_recall::startup_trace::ready()) return;

        const auto clothing = self_recall::pose_render::clothingReport();
        if (clothing && clothing != g_lastClothingReport) {
            g_lastClothingReport = clothing;
            self_recall::startup_trace::mark("50 current-equipment", clothing,
                                            self_recall::pose_session::active());
        }

        const auto count = g_npadReturnsAfterTrace.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count == 1 || count == 60 || count == 600)
            self_recall::startup_trace::mark("42 npad-module-return", count,
                                             reinterpret_cast<std::uintptr_t>(device));
    }
};
}

extern "C" void exl_main(void*, void*) {
    exl::hook::Initialize();
    const uintptr_t mainBase = exl::util::modules::GetTargetStart();
    self_recall::pose_session::initialize();
    const auto& module = wwpg::modules::selfRecall();
    module.init(mainBase);
    module.enter();
    self_recall::game_clock::install(mainBase);
    self_recall::pose_recorder::install(mainBase);
    self_recall::pose_render::install(mainBase);
    self_recall::palette::install(mainBase);
    self_recall::native_path::install(mainBase);
    self_recall::glider_release::install(mainBase);
    self_recall::native_gameplay::install();
    self_recall::camera::install();
    self_recall::vehicle::install(mainBase);
    self_recall::frame::install({preparePoseStorage, self_recall::pose_recorder::modelsComplete,
                                self_recall::pose_recorder::prepareScene});
    RayCastWorkerHook::InstallAtOffset(kRayCastWorker);
    NpadCalcHook::InstallAtOffset(kNpadCalc);
    Logging.Log("[self-recall] v1.0.12 storage=%s: Glide outfit selects 1.25/1.5/2/4x Recall speed",
                self_recall::pure::kStorageProfileName);
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
