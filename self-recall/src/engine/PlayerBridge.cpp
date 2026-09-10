
#include "PlayerBridge.hpp"
#include "RecallActorModelView.hpp"

#include <lib.hpp>
#include "RecallGliderRelease.hpp"
#include "RecallGameClock.hpp"
#include "RecallMotionContinuity.hpp"

#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Scene.hpp"
#include "totk/engine/Totk121Offsets.hpp"
#include "totk/engine/Transform.hpp"

namespace self_recall::world {
namespace {

namespace off {
constexpr std::ptrdiff_t kPlayerSetLinearVelocity = 0x01621CBC;
constexpr std::ptrdiff_t kGameUIModuleIndirect = 0x0462EC80;
constexpr std::ptrdiff_t kCheckArmorSeries = 0x0156B12C;
}  // namespace off

namespace comp {
constexpr std::ptrdiff_t kPlayer = 0x3A8;
constexpr std::ptrdiff_t kPerimeter = 0x388;
}  // namespace comp

namespace peri {
constexpr std::ptrdiff_t kFlags = 0x294;
constexpr std::uint32_t kClimbEngaged = 0x08;
}  // namespace peri

constexpr std::ptrdiff_t kUiPlayerState = 1692;

PlayerBridgeState g_state{};

totk::engine::TransformService& transforms() {
    static totk::engine::TransformService service{
        totk::engine::TransformFunctions::fromMainBase(g_state.mainBase)};
    return service;
}

totk::engine::ActorHandle playerHandle() {
    if (!totk::engine::isPlausibleAddress(g_state.playerAddress)) return {};
    const auto namePointer = totk::engine::readMemory<std::uintptr_t>(
        g_state.playerAddress + totk::engine::layout::kActorNamePointer);
    return totk::engine::ActorHandle{g_state.playerAddress, namePointer,
                                     g_state.sceneToken};
}

std::uintptr_t componentRegistry() {
    if (!totk::engine::isPlausibleAddress(g_state.playerAddress)) return 0;
    const auto registry = totk::engine::readMemory<std::uintptr_t>(
        g_state.playerAddress + totk::engine::layout::kActorComponentRegistry);
    return totk::engine::isPlausibleAddress(registry) ? registry : 0;
}

}  // namespace

void initialize(std::uintptr_t mainBase) {
    g_state.mainBase = mainBase;
    (void)transforms();
}

const PlayerBridgeState& state() { return g_state; }

void* playerActor() {
    return reinterpret_cast<void*>(g_state.playerAddress);
}

void onGenerationInvalidated() {
    g_state.stablePlayerTicks = 0;
    g_state.havePosition = false;
    g_state.lastGameTime = 0;
}

RefreshOutcome refresh(InvalidateFn invalidate, float teleportResetMeters) {
    RefreshOutcome outcome{};

    const auto scene = totk::engine::resolveScene(g_state.mainBase);
    if (!scene) {
        if (g_state.sceneToken.isValid() || g_state.playerAddress != 0) {
            invalidate("scene roster unavailable");
        }
        g_state.sceneToken = {};
        g_state.playerAddress = 0;
        return outcome;
    }

    if (scene.value.token != g_state.sceneToken) {
        invalidate("scene generation changed");
        g_state.sceneToken = scene.value.token;
    }

    const auto found = totk::engine::findResidentActor(scene.value, "Player");
    const std::uintptr_t playerAddress = found ? found.value.address : 0;
    if (playerAddress != g_state.playerAddress) {
        invalidate("player incarnation changed");
        g_state.playerAddress = playerAddress;
    } else if (playerAddress != 0 && g_state.stablePlayerTicks < 1000000) {
        ++g_state.stablePlayerTicks;
    }

    if (g_state.playerAddress == 0) return outcome;

    if (!readPose(outcome.pose)) {
        outcome.poseFinite = false;
        return outcome;
    }

    pure::GameTimeSnapshot clock;
    const bool haveClock = game_clock::snapshot(clock);
    float velocity[3]{};
    const bool haveVelocity = readEngineVelocity(velocity);
    if (g_state.havePosition) {
        const double seconds = haveClock && g_state.lastGameTime &&
            clock.elapsedNanoseconds > g_state.lastGameTime
            ? static_cast<double>(clock.elapsedNanoseconds - g_state.lastGameTime) / 1e9 : 0;
        if (pure::motionDiscontinuity(g_state.lastPosition, outcome.pose.position,
                g_state.lastVelocity, haveVelocity ? velocity : nullptr, seconds, teleportResetMeters)) {
            invalidate("teleport/warp discontinuity");
        }
    }
    g_state.lastPosition = outcome.pose.position;
    std::memcpy(g_state.lastVelocity, velocity, sizeof(velocity));
    g_state.lastGameTime = haveClock ? clock.elapsedNanoseconds : 0;
    g_state.havePosition = true;
    outcome.resolved = true;
    return outcome;
}

bool readPlayerIdentity(PlayerIdentity& out) {
    const auto* player = playerActor();
    if (!player) return false;
    out.address = reinterpret_cast<std::uintptr_t>(player);
    out.incarnation = actor_model::read<std::uint32_t>(player, actor_model::kActorId);
    return true;
}

bool readPose(pure::Pose& out) {
    const auto read = transforms().read(playerHandle(), g_state.sceneToken);
    if (!read) return false;
    out = read.value;
    return true;
}

bool readOutfit(const pure::OutfitSpeedProfile& profile, pure::OutfitSnapshot& out) {
    const auto registry = componentRegistry();
    if (!registry || !g_state.mainBase) return false;
    const auto equipment = totk::engine::readMemory<std::uintptr_t>(
        registry + actor_model::kEquipmentUser);
    if (!totk::engine::isPlausibleAddress(equipment)) return false;

    const auto linkId = [equipment](unsigned slot) {
        return totk::engine::readMemory<std::uint32_t>(equipment + 0xE0 + slot * 0x18 + 0x10);
    };
    std::array<std::uint32_t, 3> before{};
    for (unsigned slot = 0; slot < 3; ++slot) before[slot] = linkId(slot);
    pure::OutfitSnapshot next{};
    using CheckSeries = std::uint64_t (*)(std::uintptr_t, unsigned, const char* const*);
    const auto check = reinterpret_cast<CheckSeries>(g_state.mainBase + off::kCheckArmorSeries);
    for (unsigned slot = 0; slot < 3; ++slot) {
        const char* series = profile.seriesBySlot[slot];
        if (series && (check(equipment, slot, &series) & 1)) {
            next.matchedSlots |= 1u << slot;
            next.actorIds[slot] = before[slot];
        }
    }
    for (unsigned slot = 0; slot < 3; ++slot)
        if (linkId(slot) != before[slot]) return false;
    out = next;
    return true;
}

bool forcePose(const pure::Pose& pose) {
    if (transforms().force(playerHandle(), g_state.sceneToken, pose, 0U) !=
        totk::engine::TransformError::None) return false;
    g_state.lastPosition = pose.position;
    g_state.havePosition = true;
    return true;
}

bool readEngineVelocity(float out[3]) {
    if (!totk::engine::isPlausibleAddress(g_state.playerAddress)) return false;
    const auto* velocity = reinterpret_cast<const float*>(
        g_state.playerAddress + totk::engine::layout::kActorLinearVelocity);
    for (int i = 0; i < 3; ++i) out[i] = velocity[i];
    return true;
}

float liveSpeed() {
    float velocity[3]{};
    if (!readEngineVelocity(velocity)) return 0.0f;
    float sum = 0.0f;
    for (float component : velocity) {
        const float value = std::isfinite(component) ? component : 0.0f;
        sum += value * value;
    }
    return std::sqrt(sum);
}

bool clearLinearVelocity() {
    if (g_state.playerAddress == 0 || g_state.mainBase == 0) return false;
    const auto registry = componentRegistry();
    if (registry == 0) return false;
    const auto playerComponent =
        totk::engine::readMemory<std::uintptr_t>(registry + comp::kPlayer);
    if (!totk::engine::isPlausibleAddress(playerComponent)) return false;
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const auto setVelocity =
        reinterpret_cast<void (*)(std::uintptr_t, const float*, bool)>(
            g_state.mainBase + off::kPlayerSetLinearVelocity);
    setVelocity(playerComponent, zero, false);
    return true;
}

bool climbSensorEngaged() {
    const auto registry = componentRegistry();
    if (registry == 0) return false;
    const auto analyser =
        totk::engine::readMemory<std::uintptr_t>(registry + comp::kPerimeter);
    if (!totk::engine::isPlausibleAddress(analyser)) return false;
    return (totk::engine::readMemory<std::uint32_t>(analyser + peri::kFlags) &
            peri::kClimbEngaged) != 0;
}

bool nativeClimbing() {
    if (!totk::engine::isPlausibleAddress(g_state.playerAddress)) return false;
    const auto id = totk::engine::readMemory<std::uint32_t>(g_state.playerAddress + 0x10);
    return glider_release::nativeClimbing(id);
}

bool readUiPlayerState(std::uint32_t& out) {
    const auto holder = totk::engine::readMemory<std::uintptr_t>(
        g_state.mainBase + off::kGameUIModuleIndirect);
    const auto module = totk::engine::isPlausibleAddress(holder)
                            ? totk::engine::readMemory<std::uintptr_t>(holder)
                            : 0;
    if (!totk::engine::isPlausibleAddress(module)) return false;
    out = totk::engine::readMemory<std::uint32_t>(module + kUiPlayerState);
    return true;
}

bool readUprightY(float& out) {
    if (!totk::engine::isPlausibleAddress(g_state.playerAddress)) return false;
    const auto* rotation = reinterpret_cast<const float*>(
        g_state.playerAddress + totk::engine::layout::kActorRotation);
    out = rotation[4];
    return true;
}

}  // namespace self_recall::world
