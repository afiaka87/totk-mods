#pragma once

#include <cstdint>

namespace self_recall::offsets121 {

namespace equipment_archive {
inline constexpr std::uintptr_t kMarkMaterialParametersDirty = 0x02218C1C;
inline constexpr std::uintptr_t kRequestModelRetirement = 0x01376A2C;
inline constexpr std::uintptr_t kGpuPoolManagerSlot = 0x0462EF98;
inline constexpr std::uintptr_t kCalculateModelBufferSize = 0x0093F79C;
inline constexpr std::uintptr_t kMemoryBufferVtableSlot = 0x0462EEC8;
inline constexpr std::uintptr_t kInitializeMemoryBuffer = 0x00FE4D0C;
inline constexpr std::uintptr_t kCreateModelRoot = 0x0093DDA8;
inline constexpr std::uintptr_t kAppendModelResource = 0x0093DA84;
inline constexpr std::uintptr_t kConfigureModelViews = 0x015B84E0;
inline constexpr std::uintptr_t kConfigureModelDrawFlags = 0x015B852C;
inline constexpr std::uintptr_t kBindModelScene = 0x00CC423C;
inline constexpr std::uintptr_t kDestroyModelDependencies = 0x01020560;
}

namespace equipment_effects {
inline constexpr std::uintptr_t kGetXLinkComponent = 0x01066DA0;
inline constexpr std::uintptr_t kIsEventHandleValid = 0x00D17C80;
inline constexpr std::uintptr_t kEventPoolBasesSlot = 0x0462F290;
inline constexpr std::uintptr_t kEventPoolStridesSlot = 0x0462F298;
inline constexpr std::uintptr_t kBindIndirectProperty = 0x00D76F94;
inline constexpr std::uintptr_t kIsLoopingExecutor = 0x02290B00;
inline constexpr std::uintptr_t kPreActorUserVtable = 0x0460EDE0;
inline constexpr std::uintptr_t kCreateUser = 0x00D7769C;
inline constexpr std::uintptr_t kCalculateEffect = 0x007C7104;
inline constexpr std::uintptr_t kDestroyExecutor = 0x02A5F1E4;
inline constexpr std::uintptr_t kConstructPreActorUser = 0x015F154C;
inline constexpr std::uintptr_t kInitializePreActorUser = 0x00D1C7F4;
inline constexpr std::uintptr_t kResetPreActorUser = 0x00D1C388;
inline constexpr std::uintptr_t kEffectExecutorVtable = 0x045CDC38;
inline constexpr std::uintptr_t kSetBoneMatrix = 0x0229A2FC;
inline constexpr std::uintptr_t kFinalizePreActorUser = 0x008CB400;
inline constexpr std::uintptr_t kDestroyPreActorUser = 0x024BEEEC;
inline constexpr std::uintptr_t kKillEvent = 0x00BBCCAC;
inline constexpr std::uintptr_t kEmitEvent = 0x00D179E8;
inline constexpr std::uintptr_t kRequestUserCalculation = 0x00EA4958;
}

namespace palette {
inline constexpr std::uintptr_t kDrawMonochromeFilter = 0x00C31DDC;
inline constexpr std::uintptr_t kPrimitiveTexturesSlot = 0x04634AE0;
inline constexpr std::uintptr_t kFindMaterialParameter = 0x02A408C4;
inline constexpr std::uintptr_t kModelVtable = 0x045C0570;
inline constexpr std::uintptr_t kCharacterSaturationReturn = 0x009AB480;
inline constexpr std::uintptr_t kWorldSaturationReturn = 0x009AB4E0;
inline constexpr std::uintptr_t kBindUniformSlot = 0x04617088;
inline constexpr std::uintptr_t kCalculateMaterial = 0x0076B974;
inline constexpr std::uintptr_t kMapUniform = 0x0093F32C;
inline constexpr std::uintptr_t kInitializeMainScene = 0x00C77B04;
inline constexpr std::uintptr_t kWriteSaturation = 0x02A40B74;
inline constexpr std::uintptr_t kPrepareShapeDraw = 0x02218F9C;
inline constexpr std::uintptr_t kDrawModelShape = 0x0074C284;
inline constexpr std::uintptr_t kShaderGate = 0x0074C360;
inline constexpr std::uintptr_t kCompareSceneBinding = 0x0074D500;
inline constexpr std::uintptr_t kSceneBindingAddress = 0x0074D584;
}

namespace pose_render {
inline constexpr std::uintptr_t kBeforeDraw = 0x0076B6EC;
inline constexpr std::uintptr_t kCalculateView = 0x0076C078;
inline constexpr std::uintptr_t kCalculateSkeleton = 0x000824A8;
inline constexpr std::uintptr_t kCalculateShape = 0x0074F450;
inline constexpr std::uintptr_t kShapeIsVisible = 0x02A43014;
inline constexpr std::uintptr_t kCalculateBounding = 0x00756E98;
inline constexpr std::uintptr_t kRequestDraw = 0x009960D8;
inline constexpr std::uintptr_t kShapeDraw = 0x0074C284;
inline constexpr std::uintptr_t kShapeArrayDraw = 0x02A4A228;
}

namespace pose_recorder {
inline constexpr std::uintptr_t kActorUpdateMatrix = 0x0082133C;
inline constexpr std::uintptr_t kControllerMatrixAndVelocity = 0x008250F8;
inline constexpr std::uintptr_t kResidentPlayerLink = 0x00B7EF98;
inline constexpr std::uintptr_t kModelControllerGetParent = 0x015E0C34;
}
}

#include "RecallBase.hpp"
#include "totk/core/Types.hpp"

namespace self_recall::world {

using InvalidateFn = void (*)(const char* reason);

struct PlayerBridgeState {
    std::uintptr_t mainBase = 0;
    totk::core::SceneToken sceneToken{};
    std::uintptr_t playerAddress = 0;
    int stablePlayerTicks = 0;
    bool havePosition = false;
    totk::core::WorldPosition lastPosition{};
    float lastVelocity[3]{};
    std::uint64_t lastGameTime = 0;
};

struct RefreshOutcome {
    bool resolved = false;
    bool poseFinite = true;
    pure::Pose pose{};
};

void initialize(std::uintptr_t mainBase);
const PlayerBridgeState& state();

[[nodiscard]] inline bool havePlayer() { return state().playerAddress != 0; }

void* playerActor();

struct PlayerIdentity {
    std::uintptr_t address = 0;
    std::uint32_t incarnation = 0;
};
bool readPlayerIdentity(PlayerIdentity& out);

bool readOutfit(const pure::OutfitSpeedProfile& profile, pure::OutfitSnapshot& out);

RefreshOutcome refresh(InvalidateFn invalidate, float teleportResetMeters);

void onGenerationInvalidated();

bool readPose(pure::Pose& out);
bool forcePose(const pure::Pose& pose);

float liveSpeed();
bool readEngineVelocity(float out[3]);
bool clearLinearVelocity();

bool climbSensorEngaged();
bool nativeClimbing();
bool readUiPlayerState(std::uint32_t& out);
bool readUprightY(float& out);

}

#include "totk/engine/Npad.hpp"

namespace self_recall::input {

constexpr std::uint64_t kButtonB = 1ull << 1;

constexpr std::uint8_t kHoldTicks = 45;

totk::engine::NpadFrame read(void* device);

}

namespace self_recall::frame {

struct CompletedModelPhase {
    void* scene = nullptr;
    void* queue = nullptr;
    std::uint64_t epoch = 0;
    std::uint32_t queuedGroups = 0;
    std::uint32_t queuedSingles = 0;
};

struct Observers {
    void (*beginFrame)(std::uint64_t epoch) = nullptr;
    void (*modelsComplete)(const CompletedModelPhase&) = nullptr;
    void (*prepareScene)(void* scene, std::uint64_t epoch) = nullptr;
};

void install(Observers observers = {});
std::uint64_t epoch();

}

namespace self_recall::game_clock {

void install(std::uintptr_t mainBase);
bool snapshot(pure::GameTimeSnapshot& out);

}

namespace self_recall::camera { void install(); }

namespace self_recall::vehicle {
void install(std::uintptr_t mainBase);
void resetWorld();
bool controlStickActive(std::uintptr_t mainBase, const void* player);
bool controlStickRiding(std::uintptr_t mainBase, const void* player);
bool unmounted(const void* player);
bool detachControlStick(std::uintptr_t mainBase, void* player);
}

namespace self_recall::glider_release {
void install(std::uintptr_t mainBase);
bool nativeGliding(std::uint32_t actorId);
bool nativeClimbing(std::uint32_t actorId);
void request(void* player, std::uint32_t worldGeneration, std::uint64_t tick,
             bool recordedNativeGlide);
void service(void* player, std::uint32_t worldGeneration, std::uint64_t tick,
             bool allowed, bool userCancelled);
void cancel(pure::GliderReleaseEnd reason = pure::GliderReleaseEnd::Cancelled);
void resetWorld();
}

namespace self_recall::native_gameplay {
void install();
pure::StaminaStatus stamina(const void* player);
bool begin(const void* player);
void release();
void reset();
}
