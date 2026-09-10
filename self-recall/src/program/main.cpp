#include <lib.hpp>

#include "RecallFrameHooks.hpp"
#include "RecallGameClock.hpp"
#include "RecallPoseStorage.hpp"
#include "RecallPoseRecorder.hpp"
#include "RecallPoseSession.hpp"
#include "RecallPoseRender.hpp"
#include "RecallScenePalette.hpp"
#include "RecallNativePath.hpp"
#include "RecallNativeGameplay.hpp"
#include "RecallCamera.hpp"
#include "RecallVehicle.hpp"
#include "RecallGliderRelease.hpp"
#include "modules/self-recall/SelfRecallModule.hpp"

namespace {
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
        const u64 result =
            Orig(from, to, object, out, mask, flag);
        const auto& module = wwpg::modules::selfRecall();
        if (module.onRaycast) {
            module.onRaycast(&OriginalThunk, from, to, object,
                             out, mask, flag);
        }
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(NpadCalcHook) {
    static void Callback(void* device) {
        Orig(device);
        wwpg::modules::selfRecall().tick(device);
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
    Logging.Log("[self-recall] v1.0.5: Glide outfit selects 1.25/1.5/2/4x Recall speed");
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
