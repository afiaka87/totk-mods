#include <lib.hpp>

#include "RecallRuntimeEngine.hpp"
#include "GameProfiles.hpp"
#include "RecallModelEngine.hpp"
#include "RecallEffectsEngine.hpp"
#include "RecallGraphicsEngine.hpp"
#include "RecallBase.hpp"
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

HOOK_DEFINE_TRAMPOLINE(RayCastWorkerHook) {
    static u64 OriginalThunk(const void* from, const void* to,
                             const void* object, const void* out,
                             u32 mask, u32 flag) {
        return Orig(from, to, object, out, mask, flag);
    }

    static u64 Callback(const void* from, const void* to,
                        const void* object, const void* out,
                        u32 mask, u32 flag) {
        const u64 result = Orig(from, to, object, out, mask, flag);
        const auto& module = wwpg::modules::selfRecall();
        if (module.onRaycast)
            module.onRaycast(&OriginalThunk, from, to, object, out, mask, flag);
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
    const std::size_t textSize = exl::util::GetMainModuleInfo().m_Text.m_Size;
    const auto* game = self_recall::profiles::activate(mainBase, textSize);
    if (!game) {
        Logging.Log("[self-recall] unknown TotK game build; hooks disabled");
        return;
    }
    if (!self_recall::pose_render::sitesValid(mainBase, textSize) ||
        !self_recall::palette::sitesValid(mainBase, textSize) ||
        !self_recall::native_path::sitesValid(mainBase, textSize) ||
        !self_recall::equipment::sitesValid(mainBase, textSize) ||
        !self_recall::frame::sitesValid(mainBase, textSize) ||
        !self_recall::pose_recorder::sitesValid(mainBase, textSize)) {
        Logging.Log("[self-recall] native hooks differ from selected game build; hooks disabled");
        return;
    }
    totk::engine::Totk121Offsets::kSceneModuleInstance.value =
        self_recall::profiles::address(0x04728538);
    totk::engine::Totk121Offsets::kForceSetMatrix.value =
        self_recall::profiles::address(0x006BA86C);
    if (game->version == self_recall::profiles::Version::V100) {
        totk::engine::layout::kActorNamePointer = 0x210;
        totk::engine::layout::kActorComponentRegistry = 0x220;
        totk::engine::layout::kActorPosition = 0x2AC;
        totk::engine::layout::kActorRotation = 0x2B8;
        totk::engine::layout::kActorLinearVelocity = 0x318;
    }
    if (self_recall::profiles::newerRenderer())
        self_recall::model::g_nativeModelLayout = {
            0xD8, 0x110, 0x108, 0x10A, 0x118, 0x120, 0xE0, 0xE8, 0x2E0, 0x2FD};
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
    RayCastWorkerHook::InstallAtOffset(self_recall::profiles::address(0x00858590));
    NpadCalcHook::InstallAtOffset(self_recall::profiles::address(0x02A267BC));
    Logging.Log("[self-recall] v1.1.0 TotK=%s storage=%s: Glide outfit selects 1.25/1.5/2/4x Recall speed",
                game->name, self_recall::pure::kStorageProfileName);
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
