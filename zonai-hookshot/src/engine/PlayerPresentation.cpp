// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "PlayerPresentation.hpp"
#include "PlayerPresentationProfiles.hpp"
#include "TravelBodyReadShift.hpp"
#include "HandheldPose.hpp"
#include "RestingLeftArm.hpp"
#include "TravelGlidePose.hpp"
#include "TravelBodyAnchor.hpp"
#include "ArrowFlightPresentation.hpp"
#include "HandheldEffects.hpp"
#include "HookshotWorld.hpp"
#include "HookshotInput.hpp"
#include "../program/modules/zonai-hookshot/HookshotRuntime.hpp"
#include "../../../arrowbound/src/program/modules/arrowbound/HookshotRuntime.hpp"
#include <arrowbound/ActiveGame.hpp>
#include <ModelTrace.hpp>
#include <lib.hpp>
#include <nn/os.h>
#include <cstring>

namespace zonai_hookshot::playerPresentation {
namespace {
using namespace pure;
namespace pose = pure::handheld;
using pose::Matrix;
std::uintptr_t g_base{};
const profiles::Profile* g_profile{};
bool g_installed{};
nn::os::MutexType g_lock{};

// Model pointers are identity tokens until matched to the current engine callback.
struct Control {
    std::uintptr_t scene{}, actor{};
    void* player{};
    void* body{};
    void* bodySkeleton{};
    void* ragdollStructure{};
    void* weaponEffects[2]{};
    void* playerSounds{};
    void* animation{};
    void* glowUnits[48]{};
    unsigned glowCount{};
    void* hiddenUnits[16]{};
    bool isGlider[16]{};
    unsigned hiddenCount{};
    std::uint32_t generation{};
    float glowFrame{};
    Vec3 target{}, forward{}, position{}, cameraForward{};
    unsigned npad{};
    bool npadValid{}, trackHand{}, poseArm{}, glideAim{};
    bool active{}, hideGlider{}, requested{}, aiming{}, lowerLeftArm{};
    void* glideBoard{};
    unsigned glideMagnitude{}, glideDirection{};
    TravelGlideBlend glideBlend{};
    bool anchorTravel{};
    std::uintptr_t arrowToken{};
    unsigned arrowShot{};
    float arrowAnimationWeight{};
    bool alignArrowModels{};
};
Control g_control{};
struct Result {
    Vec3 hand{};
    std::uint32_t generation{};
    std::uint64_t time{};
    bool ready{};
};
Result g_result{};
std::atomic<unsigned> g_failuresLogged{};
pose::EffectMasks<64> g_effectMasks;
std::atomic<std::uintptr_t> g_climbingPlayer{};
std::atomic<void*> g_glideBoard{};
std::atomic<unsigned> g_glideMagnitude{}, g_glideDirection{};
std::atomic<std::uintptr_t> g_shiftThread{};
TravelBodyReadShift g_bodyReadShift{};
struct SavedVisibility {
    void* unit{};
    void* skeleton{};
    unsigned materials[8]{};
    bool active{};
};
SavedVisibility g_visibilitySlots[32]{};
std::uintptr_t g_visibilityScene{}, g_visibilityActor{};
void reject(unsigned reason, unsigned detail = 0) {
    const unsigned bit = 1u << reason;
    if (!(g_failuresLogged.fetch_or(bit, std::memory_order_relaxed) & bit))
        Logging.Log("[zonai-hookshot] PLAYER_PRESENTATION refused reason=%u detail=%u", reason,
                    detail);
}

template <class T> T read(const void* p, std::size_t offset) {
    T out{};
    const auto at = reinterpret_cast<std::uintptr_t>(p) + offset;
    if (p && at >= 0x1000 && ((at + sizeof(T) - 1) >> 39) == 0)
        std::memcpy(&out, reinterpret_cast<const void*>(at), sizeof(T));
    return out;
}
template <class F> F native(std::uintptr_t offset) { return reinterpret_cast<F>(g_base + offset); }
bool snapshot(Control& out) {
    if (!g_installed)
        return false;
    nn::os::LockMutex(&g_lock);
    out = g_control;
    nn::os::UnlockMutex(&g_lock);
    return true;
}
struct Reference {
    void* actor{};
    std::uint8_t counted{}, padding[7]{};
    ~Reference() {
        if (actor)
            native<void (*)(Reference*, const void*)>(g_profile->functions.releaseReference.offset)(
                this, nullptr);
    }
};
static_assert(sizeof(Reference) == 16);
void* components(const void* actor) {
    return read<void*>(actor, arrowbound::profiles::active()->layout.actorRegistry);
}
void* animation(const void* actor) {
    return read<void*>(read<void*>(read<void*>(components(actor), 0x40), 0x18), 0x18);
}
const char* currentCommand(void* controller, unsigned slot) {
    if (!controller || slot >= read<unsigned>(controller, 0x18))
        return "";
    const auto* name = native<const char* (*)(void*, unsigned)>(
        g_profile->functions.currentCommand.offset)(controller, slot);
    return name ? name : "";
}
void* effectUser(void* actor) {
    if (!actor)
        return nullptr;
    return read<void*>(native<void* (*)(void*)>(g_profile->functions.xlinkComponent.offset)(actor),
                       0x70);
}
void* modelRoot(const void* actor) {
    return read<void*>(read<void*>(components(actor), 0x10), 0x28);
}
void addHiddenUnits(Control& c, void* root, bool glider) {
    if (!root)
        return;
    const auto n = read<unsigned>(root, 0x20);
    auto* entries = read<void**>(root, 0x28);
    if (!entries || n > 32) {
        reject(12, n);
        return;
    }
    for (unsigned i = 0; i < n && c.hiddenCount < 16; ++i) {
        auto* unit = read<void*>(read<void*>(entries, 8 * i), 0);
        if (unit) {
            bool duplicate = false;
            for (unsigned j = 0; j < c.hiddenCount; ++j)
                duplicate |= c.hiddenUnits[j] == unit;
            if (!duplicate) {
                c.hiddenUnits[c.hiddenCount] = unit;
                c.isGlider[c.hiddenCount++] = glider;
            }
        }
    }
    if (c.hiddenCount == 16 && n > 1)
        reject(13, n);
}
void* firstUnit(void* root) {
    if (read<unsigned>(root, 0x20) == 0)
        return nullptr;
    auto* entries = read<void*>(root, 0x28);
    return read<void*>(read<void*>(entries, 0), 0);
}
void addGlowUnits(Control& c, void* root) {
    const auto count = read<unsigned>(root, 0x20);
    auto* entries = read<void**>(root, 0x28);
    if (!entries || count > 32)
        return;
    for (unsigned i = 0; i < count && c.glowCount < 48; ++i) {
        auto* unit = read<void*>(read<void*>(entries, 8 * i), 0);
        bool known = !unit;
        for (unsigned j = 0; j < c.glowCount; ++j)
            known |= c.glowUnits[j] == unit;
        if (!known)
            c.glowUnits[c.glowCount++] = unit;
    }
}
unsigned bone(void* unit, const char* name) {
    return native<unsigned (*)(void*, const char* const*)>(g_profile->functions.boneIndex.offset)(
        unit, &name);
}
Matrix matrix(void* unit, unsigned index) {
    Matrix out{};
    native<void (*)(void*, Matrix*, unsigned)>(g_profile->functions.boneWorld.offset)(unit, &out,
                                                                                      index);
    return out;
}
bool arrowStillOwned(const Control& c) {
    if (!c.arrowToken || !c.anchorTravel) return false;
    const auto& rt = arrowbound::runtime();
    return rt.arrow.controllerToken.load(std::memory_order_acquire) == c.arrowToken &&
           rt.arrow.shotSeq.load(std::memory_order_acquire) == c.arrowShot &&
           rt.arrow.playerActor.load(std::memory_order_acquire) == c.actor &&
           rt.drive.presentParaglider.load(std::memory_order_acquire) &&
           rt.drive.parasailActive.load(std::memory_order_acquire);
}
bool aimArmLocals(void* unit, const Control& c) {
    auto* skeleton = read<void*>(unit, g_profile->model.skeleton);
    auto* resource = read<void*>(skeleton, 0);
    auto* records = read<void*>(resource, 0x10);
    const unsigned count = read<std::uint16_t>(resource, 0x38);
    if (!records || !count || count > 256) {
        reject(1, count);
        return false;
    }
    const unsigned arm = bone(unit, "Arm_1_R"), wrist = bone(unit, "Wrist_R");
    if (arm >= count || wrist >= count) {
        reject(2, count);
        return false;
    }
    const unsigned armParent = read<std::uint16_t>(records, arm * 0x58 + 0x22);
    const unsigned wristParent = read<std::uint16_t>(records, wrist * 0x58 + 0x22);
    if (armParent >= count || wristParent >= count) {
        reject(3, count);
        return false;
    }
    Matrix original{};
    Vec3 armScale{}, wristScale{};
    const auto get =
        native<void (*)(void*, Matrix*, Vec3*, unsigned)>(g_profile->functions.boneLocal.offset);
    get(unit, &original, &armScale, arm);
    get(unit, &original, &wristScale, wrist);
    pose::ArmLocals result{};
    pose::AimCone cone{};
    const auto shoulder = matrix(unit, arm);
    if (!pose::constrainAim(c.forward, sub(c.target, pose::position(shoulder)), cone))
        return false;
    const float range = distance(c.target, pose::position(shoulder));
    const Vec3 boundedTarget =
        add(pose::position(shoulder), mul(cone.direction, range > 2.f ? range : 2.f));
    if (!pose::aimLocals(matrix(unit, armParent), matrix(unit, arm), matrix(unit, wristParent),
                         matrix(unit, wrist), armScale, wristScale, boundedTarget, result)) {
        reject(4);
        return false;
    }
    const auto set = native<void (*)(void*, const Matrix*, const Vec3*, unsigned)>(
        g_profile->functions.setBoneLocal.offset);
    set(unit, &result.shoulder, &armScale, arm);
    set(unit, &result.wrist, &wristScale, wrist);

    return true;
}
bool lowerLeftArm(void* unit) {
    const auto count = read<std::uint16_t>(read<void*>(read<void*>(unit, g_profile->model.skeleton), 0), 0x38);
    unsigned indices[std::size(pose::kRestingLeftArm)]{};
    // Resolve all names before changing anything; equipment models can differ.
    for (unsigned i = 0; i < std::size(indices); ++i) {
        indices[i] = bone(unit, pose::kRestingLeftArm[i].name);
        if (indices[i] >= count) {
            reject(16, i);
            return false;
        }
    }
    const auto get =
        native<void (*)(void*, Matrix*, Vec3*, unsigned)>(g_profile->functions.boneLocal.offset);
    const auto set = native<void (*)(void*, const Matrix*, const Vec3*, unsigned)>(
        g_profile->functions.setBoneLocal.offset);
    for (unsigned i = 0; i < std::size(indices); ++i) {
        Matrix live{};
        Vec3 scale{};
        get(unit, &live, &scale, indices[i]);
        const auto relaxed = pose::restingLocal(live, pose::kRestingLeftArm[i].rotation);
        set(unit, &relaxed, &scale, indices[i]);
    }
    return true;
}

bool hideUnit(void* unit, const Control& c) {
    for (unsigned i = 0; i < c.hiddenCount; ++i)
        if (c.hiddenUnits[i] == unit) {
            return c.isGlider[i] ? c.hideGlider : c.poseArm;
        }
    return false;
}
// Each saved address is an identity only; restoration runs on that model's own callback.
void visibility(void* unit, bool override, const Control& c) {
    using Saved = SavedVisibility;
    auto& slots = g_visibilitySlots;
    auto& scene = g_visibilityScene;
    auto& actor = g_visibilityActor;
    auto* skeleton = read<void*>(unit, g_profile->model.skeleton);
    auto* visible = read<unsigned*>(unit, g_profile->model.materialVisibility);
    const unsigned count = read<std::uint16_t>(unit, g_profile->model.materialCount);
    if (!visible || count > 256) {
        if (override)
            reject(5, count);
        return;
    }
    nn::os::LockMutex(&g_lock);
    // Keep saved visibility through menus; retire it only when the scene or player changes.
    if (scene != c.scene || actor != c.actor) {
        for (auto& slot : slots)
            slot = {};
        scene = c.scene;
        actor = c.actor;
    }
    Saved* saved = nullptr;
    Saved* available = nullptr;
    for (auto& slot : slots) {
        if (slot.unit == unit && slot.skeleton == skeleton) {
            saved = &slot;
            break;
        }
        if (!slot.active)
            available = &slot;
    }
    if (!saved && override)
        saved = available;
    if (!saved) {
        nn::os::UnlockMutex(&g_lock);
        if (override)
            reject(6);
        return;
    }
    if (override && !saved->active) {
        *saved = {};
        saved->unit = unit;
        saved->skeleton = skeleton;
        saved->active = true;
        std::memcpy(saved->materials, visible, ((count + 31) / 32) * 4);
    }
    if (saved->active) {
        const auto set =
            native<void (*)(void*, unsigned, bool)>(g_profile->functions.setMaterialVisible.offset);
        for (unsigned i = 0; i < count; ++i)
            set(unit, i, override ? false : ((saved->materials[i / 32] >> (i % 32)) & 1));
        if (!override)
            saved->active = false;
    }
    nn::os::UnlockMutex(&g_lock);
}
void publishHand(void* unit, const Control& c) {
    if (!c.trackHand || unit != c.body || read<void*>(unit, g_profile->model.skeleton) != c.bodySkeleton)
        return;
    const auto count = read<std::uint16_t>(read<void*>(read<void*>(unit, g_profile->model.skeleton), 0), 0x38);
    const auto wrist = bone(unit, "Wrist_R");
    if (wrist >= count) {
        reject(7, wrist);
        return;
    }
    const Vec3 hand = pose::handOrigin(matrix(unit, wrist));
    if (!finite3(hand)) {
        reject(7);
        return;
    }
    nn::os::LockMutex(&g_lock);
    if (g_control.trackHand && g_control.generation == c.generation && g_control.body == unit) {
        g_result = {hand, c.generation, svcGetSystemTick(), true};
    }
    nn::os::UnlockMutex(&g_lock);
}
enum class Ground { None, Idle, Walk, LockOnIdle, LockOnWalk };
void turnTowardAim(void* player, const Control& c, Ground ground = Ground::None) {
    static void* owner{};
    static std::uint32_t generation{};
    static bool turning{}, lockedOn{}, ownedLower{};
    static float speed{};
    static std::uint64_t lastTime{};
    if (owner != player) {
        owner = player;
        ownedLower = false;
        turning = false;
        speed = 0;
        lastTime = 0;
    }
    if (generation != c.generation) {
        generation = c.generation;
        turning = false;
        speed = 0;
        lastTime = 0;
    }
    const auto* lower = currentCommand(c.animation, read<unsigned>(player, 0xB68));
    const bool ownLower =
        !std::strcmp(lower, "UltraHandTurnLower") || !std::strcmp(lower, "UltraHandWaitLower");
    if (!ownLower)
        ownedLower = false;
    if (!c.poseArm || !c.aiming || c.glideAim) {
        const auto* upper = currentCommand(c.animation, read<unsigned>(player, 0xB64));
        if (ownedLower && ownLower && std::strncmp(upper, "UltraHand", 9)) {
            const char* restore = lockedOn ? "LockOnWait" : "Wait";
            native<void (*)(void*, const char* const*, bool)>(
                g_profile->functions.lowerCommand.offset)(player, &restore, false);
        }
        ownedLower = false;
        turning = false;
        speed = 0;
        lastTime = 0;
        return;
    }
    // Ground callbacks establish ownership even when a full-body animation leaves the lower slot empty.
    if (ground == Ground::None)
        return;

    lockedOn = ground == Ground::LockOnIdle || ground == Ground::LockOnWalk;
    const bool idle = ground == Ground::Idle || ground == Ground::LockOnIdle;
    auto* actor = read<void*>(player, 0x18);
    auto* controller = read<void*>(read<void*>(read<void*>(components(actor), 0x50), 0x20), 0x10);
    auto* body = read<void*>(read<void*>(controller, 0x18), 0);
    auto* step = read<void*>(read<void*>(controller, 8), 0x20);
    auto* blackboard = read<void*>(c.animation, 0x40);
    if (!body || !step) {
        reject(26);
        return;
    }
    int rotateDeg = -1;
    if (idle) {
        const char* key = "RotateDeg";
        if (read<void*>(blackboard, 0xD0))
            rotateDeg = native<int (*)(void*, const char* const*)>(
                g_profile->functions.blackboardIndex.offset)(blackboard, &key);
        if (rotateDeg < 0 ||
            unsigned(rotateDeg) >= read<unsigned>(read<void*>(blackboard, 0x20), 0)) {
            reject(27, unsigned(rotateDeg));
            return;
        }
    }
    // The native turn helper handles zero and NaN timing scalars.
    pose::AimCone cone{};
    const auto position = read<Vec3>(actor, arrowbound::profiles::active()->layout.actorPosition);
    const float yaw = native<float (*)(void*)>(g_profile->functions.playerYaw.offset)(player);
    const Vec3 forward{std::sin(yaw), 0, std::cos(yaw)};
    if (!pose::constrainAim(forward, sub(c.target, position), cone)) {
        reject(28);
        return;
    }
    const bool wasTurning = turning;
    turning = pose::needsBodyTurn(cone.yaw, turning);
    const auto now = svcGetSystemTick();
    const auto frequency = nn::os::GetSystemTickFrequency();
    const float seconds = lastTime && frequency ? float(now - lastTime) / float(frequency) : 0.f;
    lastTime = now;
    Vec3 angular{};
    if (turning) {
        native<void (*)(void*, const Vec3*, Vec3*, bool, float)>(
            g_profile->functions.turnVelocity.offset)(actor, &c.target, &angular, false, 0.08f);
        if (!finite3(angular)) {
            reject(29);
            return;
        }
    }
    speed = pose::smoothTurnSpeed(speed, angular.y, seconds);
    if (turning || wasTurning || std::fabs(speed) > 0.0001f) {
        const Vec3 smooth{0, speed, 0};
        native<void (*)(void*, const Vec3*, bool)>(g_profile->functions.setAngularVelocity.offset)(
            player, &smooth, false);
    }
    if (idle && (turning || wasTurning || std::fabs(speed) > 0.0001f)) {
        native<void (*)(void*, unsigned, float)>(g_profile->functions.floatSetter.offset)(
            blackboard, rotateDeg, clamp(-speed * 57.2957795f / 30.f, -2.f, 2.f));
        const char* command =
            std::fabs(speed) > 0.0001f ? "UltraHandTurnLower" : "UltraHandWaitLower";
        native<void (*)(void*, const char* const*, bool)>(g_profile->functions.lowerCommand.offset)(
            player, &command, false);
        ownedLower = true;
    }
}
void groundAim(void* action, Ground ground) {
    Control c{};
    if (!snapshot(c) || !c.active || !c.aiming)
        return;
    auto* player = native<void* (*)(void*)>(g_profile->functions.actionPlayer.offset)(action);
    if (player == c.player)
        turnTowardAim(player, c, ground);
}
HOOK_DEFINE_TRAMPOLINE(GroundWaitHook) {
    static std::uint64_t Callback(void* action) {
        const auto result = Orig(action);
        groundAim(action, Ground::Idle);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(GroundLockOnWaitHook) {
    static std::uint64_t Callback(void* action) {
        const auto result = Orig(action);
        groundAim(action, Ground::LockOnIdle);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(GroundMoveHook) {
    static std::uint64_t Callback(void* action) {
        const auto result = Orig(action);
        groundAim(action, Ground::Walk);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(GroundLockOnMoveHook) {
    static void Callback(void* action) {
        Orig(action);
        groundAim(action, Ground::LockOnWalk);
    }
};
HOOK_DEFINE_TRAMPOLINE(PlayerAnimationHook) {
    static std::uint64_t Callback(void* player) {
        const auto result = Orig(player);
        Control c{};
        if (snapshot(c) && c.player == player) {
            turnTowardAim(player, c);
            if (!c.poseArm || c.glideAim ||
                g_climbingPlayer.load(std::memory_order_acquire) ==
                    reinterpret_cast<std::uintptr_t>(player))
                return result;
            const char* command = "UltraHandWait";
            native<void (*)(void*, const char* const*, bool)>(
                g_profile->functions.rightArmCommand.offset)(player, &command, false);
        }
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(GlideAnimationFloatHook) {
    static void* Callback(void* blackboard, unsigned index, float value) {
        // Override animation values only; native physics still uses the real stick input.
        if (blackboard == g_glideBoard.load(std::memory_order_acquire) &&
            (index == g_glideMagnitude.load(std::memory_order_relaxed) ||
             index == g_glideDirection.load(std::memory_order_relaxed))) {
            Control c{};
            if (snapshot(c) && blackboard == c.glideBoard && c.glideBlend.weight > 0 &&
                g_climbingPlayer.load(std::memory_order_acquire) !=
                    reinterpret_cast<std::uintptr_t>(c.player)) {
                value = glideAnimationValue(index, c.glideMagnitude, c.glideDirection, value,
                                            c.glideBlend);
            }
        }
        return Orig(blackboard, index, value);
    }
};
struct BodyReadScope {
    bool active{};
    BodyReadScope(void* entries, unsigned count, Vec3 shift) {
        nn::os::LockMutex(&g_lock);
        if (!g_shiftThread.load(std::memory_order_relaxed)) {
            g_bodyReadShift = {};
            g_bodyReadShift.thread = reinterpret_cast<std::uintptr_t>(nn::os::GetCurrentThread());
            g_bodyReadShift.count = count - 1;
            g_bodyReadShift.shift = shift;
            for (unsigned i = 0; i < g_bodyReadShift.count; ++i)
                g_bodyReadShift.bodies[i] =
                    reinterpret_cast<std::uintptr_t>(read<void*>(entries, 0x98u * i + 0x28));
            g_shiftThread.store(g_bodyReadShift.thread, std::memory_order_release);
            active = true;
        }
        nn::os::UnlockMutex(&g_lock);
        if (!active)
            reject(21);
    }
    ~BodyReadScope() {
        if (!active)
            return;
        nn::os::LockMutex(&g_lock);
        g_shiftThread.store(0, std::memory_order_release);
        g_bodyReadShift = {};
        nn::os::UnlockMutex(&g_lock);
    }
};
HOOK_DEFINE_TRAMPOLINE(BodyMatrixHook) {
    static std::uint64_t Callback(void* body, Matrix* matrix) {
        const auto result = Orig(body, matrix);
        if (!g_shiftThread.load(std::memory_order_acquire))
            return result;
        const auto thread = reinterpret_cast<std::uintptr_t>(nn::os::GetCurrentThread());
        if (g_shiftThread.load(std::memory_order_acquire) != thread)
            return result;
        nn::os::LockMutex(&g_lock);
        g_bodyReadShift.apply(thread, reinterpret_cast<std::uintptr_t>(body), matrix);
        nn::os::UnlockMutex(&g_lock);
        return result;
    }
};
// Keep the native merge and restoration together to preserve their call order.
HOOK_DEFINE_TRAMPOLINE(TravelBodyAnchorHook) {
    static std::uint64_t Callback(void* structure, const Matrix* basis) {
        Control c{};
        if (!snapshot(c) || !c.anchorTravel || structure != c.ragdollStructure || !basis)
            return Orig(structure, basis);
        if (c.arrowToken && !arrowStillOwned(c)) return Orig(structure, basis);
        if (c.arrowToken && c.arrowAnimationWeight == 1.f)
            return Orig(structure, basis);
        const auto* profile = arrowbound::profiles::active();
        const auto count = read<unsigned>(structure, 0x58);
        auto* entries = read<void*>(structure, 0x60);
        auto* mapping = read<void*>(structure, 0x40);
        auto* records = read<std::byte*>(mapping, 0x10);
        const auto mappedCount = read<unsigned>(mapping, 8);
        if (count < 2 || count > 65 || !mappedCount || mappedCount > 64 || !entries || !records) {
            reject(19, count);
            return Orig(structure, basis);
        }
        const char* name = "Skl_Root";
        const auto index = native<std::uint16_t (*)(void*, const char* const*)>(
            profile->physics.findBodyByName.offset)(structure, &name);
        if (index >= count - 1 || index >= mappedCount) {
            reject(23, index);
            return Orig(structure, basis);
        }
        auto* body = read<void*>(entries, 0x98u * index + 0x28);
        if (!body) {
            reject(20, index);
            return Orig(structure, basis);
        }
        Matrix physical{};
        native<void (*)(void*, Matrix*)>(profile->physics.getCurrentMatrix.offset)(body, &physical);
        const auto flags = read<unsigned>(structure, 0x10);
        auto* sourcePose = read<void*>(structure, 0x48);
        const auto poseIndex = read<std::int16_t>(records + 0x78u * index, 0);
        const bool merging =
            sourcePose && !(flags & 0x800) && read<std::int16_t>(structure, 0x16) != -1;
        Vec3 anchor{}, shift{}, cached{};
        bool correcting = false;
        if (merging) {
            if (read<std::int16_t>(records + 0x78u * index, 6) != -1 || poseIndex < 0 ||
                unsigned(poseIndex) >= read<unsigned>(sourcePose, 0x10))
                reject(24, unsigned(poseIndex));
            else {
                // Read the incoming animated anchor before native pose merging.
                auto* animated = native<const void* (*)(void*, unsigned)>(
                    g_profile->functions.animatedPose.offset)(sourcePose, unsigned(poseIndex));
                anchor = read<Vec3>(animated, 0x10);
                correcting = animated && travelBodyAnchor(*basis, anchor, pose::position(physical),
                                                          cached, shift);
                if (!correcting)
                    reject(25, unsigned(poseIndex));
            }
        }
        if (!g_profile->nativeTranslation) {
            if (!correcting)
                return Orig(structure, basis);
            // Shift only this merge's sampled matrices; no rigid-body state is written.
            BodyReadScope sampled(entries, count, shift);
            return Orig(structure, basis);
        }
        // Restore the merge translation state after the call; never move rigid bodies here.
        const auto savedCached = read<Vec3>(structure, 0x17C),
                   savedShift = read<Vec3>(structure, 0x188);
        const float savedPrimary = c.arrowToken ? read<float>(structure, 0x30) : 0.f,
                    savedSecondary = c.arrowToken ? read<float>(structure, 0x34) : 0.f;
        const float blend = pure::arrowAnimationWeight(savedPrimary, savedSecondary,
                                                       c.arrowAnimationWeight);
        const bool blendArrow = correcting && c.arrowToken && c.arrowAnimationWeight > 0 &&
                                std::isfinite(blend);
        auto* raw = static_cast<std::byte*>(structure);
        if (correcting) {
            const unsigned translatedFlags = flags | 0x400000;
            std::memcpy(raw + 0x17C, &cached, sizeof(cached));
            std::memcpy(raw + 0x10, &translatedFlags, sizeof(translatedFlags));
        }
        if (blendArrow) {
            const float primary = 1.f;
            std::memcpy(raw + 0x30, &primary, sizeof(primary));
            std::memcpy(raw + 0x34, &blend, sizeof(blend));
        }
        const auto result = Orig(structure, basis);
        if (blendArrow) {
            std::memcpy(raw + 0x30, &savedPrimary, sizeof(savedPrimary));
            std::memcpy(raw + 0x34, &savedSecondary, sizeof(savedSecondary));
        }
        if (correcting) {
            const unsigned restoredFlags =
                (read<unsigned>(structure, 0x10) & ~0x400000u) | (flags & 0x400000u);
            const auto restoredCached = (flags & 0x400000u) ? savedCached : pose::position(*basis);
            const auto restoredShift = (flags & 0x400000u) && !std::isnan(savedCached.x)
                                           ? sub(pose::position(*basis), savedCached)
                                           : savedShift;
            std::memcpy(raw + 0x10, &restoredFlags, sizeof(restoredFlags));
            std::memcpy(raw + 0x17C, &restoredCached, sizeof(restoredCached));
            std::memcpy(raw + 0x188, &restoredShift, sizeof(restoredShift));
        }
        if (correcting && c.arrowToken && std::isfinite(savedPrimary) &&
            std::isfinite(savedSecondary)) {
            static std::atomic<unsigned> samples{};
            const auto n = samples.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n <= 12 || n % 60 == 0) {
                const auto* finalPose = native<const void* (*)(void*, unsigned)>(
                    g_profile->functions.animatedPose.offset)(sourcePose, unsigned(poseIndex));
                const float rootError = finalPose ? distance(read<Vec3>(finalPose, 0x10), anchor) : -1.f;
                Logging.Log("[zonai-hookshot] ARROW_PRESENTATION shot=%u n=%u shift_cm=%d animation_milli=%d native_primary_milli=%d native_secondary_milli=%d root_error_cm=%d",
                            c.arrowShot, n, int(length(shift) * 100.f), int(blend * 1000.f),
                            int(savedPrimary * 1000.f), int(savedSecondary * 1000.f),
                            std::isfinite(rootError) ? int(rootError * 100.f) : -1);
            }
        }
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(ModelWorldHook) {
    static std::uint64_t Callback(void* unit, const Matrix* root, const float* scale) {
        Control c{};
        if (!snapshot(c))
            return Orig(unit, root, scale);
        const auto result = Orig(unit, root, scale);
        if (c.poseArm &&
            g_climbingPlayer.load(std::memory_order_acquire) !=
                reinterpret_cast<std::uintptr_t>(c.player) &&
            unit == c.body && read<void*>(unit, g_profile->model.skeleton) == c.bodySkeleton) {
            // Preserve Weapon_R for the visible glider while aiming the wrist independently.
            const auto count = read<std::uint16_t>(read<void*>(c.bodySkeleton, 0), 0x38);
            const auto attachment = c.glideAim ? bone(unit, "Weapon_R") : count;
            const bool preserve = c.glideAim && attachment < count;
            const auto nativeAttachment = preserve ? matrix(unit, attachment) : Matrix{};
            if (c.glideAim && !preserve)
                reject(15, attachment);
            const bool right = aimArmLocals(unit, c);
            const bool left = c.lowerLeftArm && lowerLeftArm(unit);
            if (right || left)
                Orig(unit, root, scale);
            if (preserve) {
                native<void (*)(void*, const Matrix*, unsigned)>(
                    g_profile->functions.setBoneWorld.offset)(unit, &nativeAttachment, attachment);
            }
        }
        visibility(unit, hideUnit(unit, c), c);
        return result;
    }
};
bool arrowRenderMiss(unsigned reason, unsigned shot, std::uintptr_t detail = 0) {
    static std::atomic<unsigned> failures{};
    const auto bit = 1u << reason;
    if (!(failures.fetch_or(bit, std::memory_order_relaxed) & bit))
        Logging.Log("[zonai-hookshot] ARROW_RENDER_MISS shot=%u reason=%u detail=%p", shot,
                    reason, reinterpret_cast<void*>(detail));
    return false;
}
void flushArrowModelRequest(void* model) {
    const auto flags = read<std::uint8_t>(model, 0x241);
    if (!(flags & 4)) return;
    // Consume the queued update before the scene starts its culling workers.
    native<void (*)(void*)>(0x2216CC4)(model);
    static_cast<std::byte*>(model)[0x241] &= ~std::byte{4};
}
bool renderedArrowTarget(const Control& c, Vec3& position, Vec3& velocity, unsigned& sample,
                         bool prepare = false) {
    arrowbound::ArrowRenderIdentity identity{};
    if (!arrowbound::runtime().arrow.renderIdentity.snapshot(identity) ||
        identity.controller != c.arrowToken || identity.shot != c.arrowShot ||
        !identity.handle || identity.generation == -1 || !arrowStillOwned(c))
        return arrowRenderMiss(0, c.arrowShot, identity.shot);
    struct StaticLink {
        std::uintptr_t vtable{}, handle{};
        std::int32_t generation{-1};
        std::uint8_t flags[4]{};
    } link{0, identity.handle, identity.generation};
    static_assert(sizeof(StaticLink) == 24);
    auto arrow = native<Reference (*)(const void*)>(g_profile->functions.getReference.offset)(&link);
    if (reinterpret_cast<std::uintptr_t>(arrow.actor) != identity.actor || !arrowStillOwned(c))
        return arrowRenderMiss(1, c.arrowShot, reinterpret_cast<std::uintptr_t>(arrow.actor));
    auto* root = modelRoot(arrow.actor);
    if (!root || read<unsigned>(root, 0x20) > 32) return arrowRenderMiss(2, c.arrowShot);
    if (prepare) flushArrowModelRequest(root);
    auto* unit = firstUnit(root);
    if (!unit || read<void*>(unit, 0) != read<void*>(c.body, 0))
        return arrowRenderMiss(3, c.arrowShot, read<std::uintptr_t>(unit, 0));
    auto* skeleton = read<void*>(unit, g_profile->model.skeleton);
    const auto bones = read<std::uint16_t>(read<void*>(skeleton, 0), 0x38);
    if (!skeleton || !bones || bones > 512) return arrowRenderMiss(4, c.arrowShot, bones);
    const auto world = matrix(unit, 0);
    for (float value : world.v)
        if (!std::isfinite(value)) return arrowRenderMiss(5, c.arrowShot);
    position = pose::position(world);
    velocity = identity.velocity;
    sample = identity.sample;
    return arrowStillOwned(c);
}
HOOK_DEFINE_TRAMPOLINE(ArrowCameraTargetHook) {
    static void Callback(void* component, const float* delta) {
        Orig(component, delta);
        Control c{};
        if (!snapshot(c) || !c.alignArrowModels || !arrowStillOwned(c)) return;
        auto* actor = read<void*>(component, 0x18);
        if (reinterpret_cast<std::uintptr_t>(actor) != c.actor ||
            firstUnit(modelRoot(actor)) != c.body ||
            read<void*>(c.body, g_profile->model.skeleton) != c.bodySkeleton) return;
        Vec3 position{}, velocity{}, target{}, focus{};
        unsigned sample{};
        const auto player = read<Vec3>(actor, arrowbound::profiles::active()->layout.actorPosition);
        const auto nativeFocus = read<Vec3>(component, 0xC8);
        if (!renderedArrowTarget(c, position, velocity, sample) ||
            !arrowbound::pure::arrowTrailPoint(position, velocity, target) ||
            !arrowCameraFocus(nativeFocus, player, target, velocity, focus)) {
            reject(29, c.arrowShot);
            return;
        }
        if (!arrowStillOwned(c)) return;
        std::memcpy(static_cast<std::byte*>(component) + 0xC8, &focus, sizeof(focus));
        static unsigned shot{}, samples{};
        if (shot != c.arrowShot) { shot = c.arrowShot; samples = 0; }
        const auto n = ++samples;
        if (n <= 120 || n % 60 == 0)
            Logging.Log("[zonai-hookshot] ARROW_CAMERA_TARGET shot=%u n=%u arrow_n=%u shift_cm=%d native_cm=(%d,%d,%d) focus_cm=(%d,%d,%d) player_cm=(%d,%d,%d) target_cm=(%d,%d,%d)",
                        c.arrowShot, n, sample, int(distance(player, target) * 100),
                        int(nativeFocus.x * 100), int(nativeFocus.y * 100), int(nativeFocus.z * 100),
                        int(focus.x * 100), int(focus.y * 100), int(focus.z * 100),
                        int(player.x * 100), int(player.y * 100), int(player.z * 100),
                        int(target.x * 100), int(target.y * 100), int(target.z * 100));
    }
};
void updateArrowModel(void* model, const Matrix& root) {
    std::memcpy(static_cast<std::byte*>(model) + 0x1F8, &root, sizeof(root));
    static_cast<std::byte*>(model)[0x240] |= std::byte{1};
    // Rebuild the native model hierarchy without repeating the actor's pose modifiers.
    native<void (*)(void*, void*)>(0x64D388)(model, nullptr);
}
void prepareArrowScene(void* scene, const Control& c) {
    if (!c.alignArrowModels || !c.player || !arrowStillOwned(c) || !world::ready()) return;
    auto* player = world::playerActor();
    if (reinterpret_cast<std::uintptr_t>(player) != c.actor) return;
    auto* body = modelRoot(player);
    if (!body || read<void*>(body, 0x60) != scene || firstUnit(body) != c.body ||
        read<void*>(c.body, g_profile->model.skeleton) != c.bodySkeleton) return;

    Reference references[22]{};
    void* models[24]{body};
    unsigned count = 1, held = 0;
    auto* gliderModel = static_cast<void*>(nullptr);
    const auto collect = [&](const void* link, bool glider) {
        if (!link || held == 22) return;
        auto part = native<Reference (*)(const void*)>(g_profile->functions.getReference.offset)(link);
        auto* model = modelRoot(part.actor);
        if (!model || read<void*>(model, 0x60) != scene) return;
        auto* unit = firstUnit(model);
        if (!unit || read<void*>(unit, 0) != read<void*>(c.body, 0) ||
            read<unsigned>(model, 0x20) > 32) { reject(30, count); return; }
        if (glider) gliderModel = model;
        for (unsigned i = 0; i < count; ++i) if (models[i] == model) return;
        references[held].actor = part.actor;
        references[held++].counted = part.counted;
        part.actor = nullptr;
        models[count++] = model;
    };
    auto* equipment = read<std::byte*>(components(player), 0x230);
    if (equipment)
        for (unsigned i = 0; i < 20; ++i) collect(equipment + 0x20 + i * 0x18, false);
    if (equipment) {
        auto sword = native<Reference (*)(const void*)>(g_profile->functions.getReference.offset)(
            equipment + 0x20);
        auto* weapon = read<void*>(components(sword.actor), 0x208);
        if (weapon && read<std::uint8_t>(weapon, 0x50C))
            collect(static_cast<std::byte*>(weapon) + 0xB0, false);
    }
    collect(static_cast<std::byte*>(c.player) + 0x6B8, true);
    if (!gliderModel) { reject(31, c.arrowShot); return; }

    for (unsigned i = 0; i < count; ++i) flushArrowModelRequest(models[i]);
    const auto oldRoot = read<Matrix>(body, 0x1F8);
    Vec3 arrow{}, velocity{}, target{}, shift{};
    unsigned sample{};
    if (!renderedArrowTarget(c, arrow, velocity, sample, true) ||
        !arrowRenderTranslation(oldRoot, arrow, velocity, target, shift)) {
        reject(8, c.arrowShot); return;
    }
    const unsigned attachment = bone(c.body, "Weapon_R");
    const auto bones = read<std::uint16_t>(read<void*>(c.bodySkeleton, 0), 0x38);
    const auto* animation = read<void*>(body, 0x40);
    if (attachment >= bones || !animation || read<int>(animation, 0x90) < 1 || !arrowStillOwned(c)) {
        reject(19, attachment); return;
    }
    const auto oldAttachment = matrix(c.body, attachment);
    const auto oldGlider = read<Matrix>(gliderModel, 0x1F8);
    Matrix inverse{};
    if (!pose::inverseAffine(oldAttachment, inverse)) { reject(23, attachment); return; }

    // Apply the current animation frame; this does not advance animation time.
    native<void (*)(void*, void*, unsigned)>(0x832640)(body, body, 1);
    updateArrowModel(body, translateArrowModel(oldRoot, shift));
    Matrix gliderRoot{};
    if (!arrowAttachmentRoot(oldAttachment, matrix(c.body, attachment), oldGlider, gliderRoot)) {
        reject(24, attachment);
        gliderRoot = translateArrowModel(oldGlider, shift);
    }
    for (unsigned i = 1; i < count; ++i) {
        const auto root = models[i] == gliderModel ? gliderRoot :
            translateArrowModel(read<Matrix>(models[i], 0x1F8), shift);
        updateArrowModel(models[i], root);
    }
    static unsigned shot{}, frames{};
    if (shot != c.arrowShot) { shot = c.arrowShot; frames = 0; }
    const auto n = ++frames;
    if (n <= 120 || n % 60 == 0) {
        const auto final = pose::position(matrix(c.body, 0));
        const auto* alpha = read<void*>(components(player), 0x280);
        const float opacity = alpha ? read<float>(alpha, 0x5C) * read<float>(alpha, 0x64) *
            read<float>(alpha, 0xA4) * read<float>(alpha, 0x60) : -1.f;
        Logging.Log("[zonai-hookshot] ARROW_SCENE shot=%u n=%u arrow_n=%u models=%u shift_cm=%d root_error_cm=%d trail_cm=%d alpha_milli=%d target_cm=(%d,%d,%d)",
                    c.arrowShot, n, sample, count, int(length(shift) * 100),
                    int(distance(final, target) * 100), int(distance(final, arrow) * 100),
                    std::isfinite(opacity) ? int(opacity * 1000) : -1,
                    int(target.x * 100), int(target.y * 100), int(target.z * 100));
    }
}
HOOK_DEFINE_TRAMPOLINE(ArrowSceneFrameHook) {
    static std::uint64_t Callback(void* scene) {
        Control c{};
        if (snapshot(c)) prepareArrowScene(scene, c);
        return Orig(scene);
    }
};
struct GlowParameters {
    struct Saved {
        unsigned material, parameter;
        float value, scale[2], translation[2];
        bool srt;
    };
    void* unit{};
    Saved saved[64];
    unsigned used{};
    void set(unsigned material, const char* name, float value) {
        const auto index = native<unsigned (*)(void*, unsigned, const char* const*)>(
            g_profile->functions.parameterIndex.offset)(unit, material, &name);
        if (index >= 256 || used == 64)
            return;
        auto& entry = saved[used++];
        entry = {};
        entry.material = material;
        entry.parameter = index;
        entry.value = native<float (*)(void*, unsigned, unsigned)>(
            g_profile->functions.parameterFloat.offset)(unit, material, index);
        native<void (*)(void*, unsigned, unsigned, float)>(
            g_profile->functions.setParameterFloat.offset)(unit, material, index, value);
    }
    void srt(unsigned material) {
        const char* name = "p_tex_srt0";
        const auto index = native<unsigned (*)(void*, unsigned, const char* const*)>(
            g_profile->functions.parameterIndex.offset)(unit, material, &name);
        if (index >= 256 || used == 64)
            return;
        auto& entry = saved[used++];
        entry = {};
        entry.material = material;
        entry.parameter = index;
        entry.srt = true;
        native<void (*)(void*, unsigned, unsigned, float*, float*, float*)>(
            g_profile->functions.parameterSrt.offset)(unit, material, index, entry.scale,
                                                      &entry.value, entry.translation);
        const float translation[2]{entry.translation[0], 1.f};
        native<void (*)(void*, unsigned, unsigned, const float*, const float*, float)>(
            g_profile->functions.setParameterSrt.offset)(unit, material, index, entry.scale,
                                                         translation, 1.57079637f);
    }
    GlowParameters(void* model, const Control& c) : unit(model) {
        if (!c.active)
            return;
        bool owned = false;
        for (unsigned i = 0; i < c.glowCount; ++i)
            owned |= c.glowUnits[i] == unit;
        const auto count = read<std::uint16_t>(unit, g_profile->model.materialCount);
        if (!owned || count > 64)
            return;
        for (unsigned m = 0; m < count; ++m) {
            const char* name = native<const char* (*)(void*, unsigned)>(
                g_profile->functions.materialName.offset)(unit, m);
            if (!name)
                continue;
            const bool marking = (!std::strncmp(name, "Mt_Monyou_", 10) ||
                                  !std::strncmp(name, "Mt_Upper_Monyou_", 16));
            if (marking) {
                set(m, "p_const_value0", pose::ultrahandPulse(c.glowFrame, 1, 20));
                set(m, "p_const_value7", 2);
                srt(m);
            } else if (!std::strcmp(name, "Mt_Upper_Skin") ||
                       !std::strncmp(name, "Mt_Upper_Skin_RaulSkin", 22))
                set(m, "p_const_value7", pose::ultrahandPulse(c.glowFrame, 2, 6));
        }
    }
    ~GlowParameters() {
        // Restore material parameters after upload so native animation can write the next frame.
        while (used) {
            const auto& e = saved[--used];
            if (e.srt)
                native<void (*)(void*, unsigned, unsigned, const float*, const float*, float)>(
                    g_profile->functions.setParameterSrt.offset)(unit, e.material, e.parameter,
                                                                 e.scale, e.translation, e.value);
            else
                native<void (*)(void*, unsigned, unsigned, float)>(
                    g_profile->functions.setParameterFloat.offset)(unit, e.material, e.parameter,
                                                                   e.value);
        }
    }
};
HOOK_DEFINE_TRAMPOLINE(ModelBeforeDrawHook) {
    static std::uint64_t Callback(void* unit, void* graphics, void* views, int count) {
        Control c{};
        if (snapshot(c)) {
            visibility(unit, hideUnit(unit, c), c);
        }
        GlowParameters glow(unit, c);
        const auto result = Orig(unit, graphics, views, count);
        if (c.alignArrowModels && unit == c.body && arrowStillOwned(c) &&
            read<void*>(unit, g_profile->model.skeleton) == c.bodySkeleton) {
            static unsigned shot{}, samples{};
            if (shot != c.arrowShot) { shot = c.arrowShot; samples = 0; }
            const auto n = ++samples;
            if (n <= 12 || n % 60 == 0) {
                const auto bounds = arrowbound::pure::inspectModelCull(unit);
                const auto root = pose::position(matrix(unit, 0));
                const unsigned bones = read<std::uint16_t>(read<void*>(c.bodySkeleton, 0), 0x38);
                float reach = 0;
                if (bones <= 512)
                    for (unsigned i = 0; i < bones; ++i)
                        reach = std::max(reach, distance(root, pose::position(matrix(unit, i))));
                Logging.Log("[zonai-hookshot] ARROW_UPLOAD shot=%u n=%u views=%08x bounds_valid=%u radius_cm=%d root_bounds_cm=%d bone_reach_cm=%d root_cm=(%d,%d,%d)",
                            c.arrowShot, n, bounds.mask, unsigned(bounds.sphereValid),
                            bounds.sphereValid ? int(bounds.radius * 100) : -1,
                            bounds.sphereValid ? int(distance(root, bounds.center) * 100) : -1,
                            std::isfinite(reach) ? int(reach * 100) : -1,
                            int(root.x * 100), int(root.y * 100), int(root.z * 100));
            }
        }
        // Publish after body upload; simulation can already be working on the next pose.
        publishHand(unit, c);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(ShapeVisibleHook) {
    static std::uint64_t Callback(void* renderUnit) {
        Control c{};
        if (snapshot(c) && hideUnit(read<void*>(renderUnit, 8), c))
            return 0;
        return Orig(renderUnit);
    }
};
void routeNativeDispatch(exl::hook::InlineFloatCtx* ctx, unsigned index) {
    auto* gp = reinterpret_cast<exl::hook::InlineCtx*>(reinterpret_cast<std::byte*>(ctx) + 0x200);
    static_assert(sizeof(exl::hook::InlineCtx) == 0xF8);
    auto* owner = reinterpret_cast<void*>(gp->X[g_profile->routes[index].ownerRegister]);
    Control c{};
    if (!owner || !snapshot(c))
        return;
    bool owned = false;
    if (index < 3) {
        owned = c.trackHand && owner == c.body;
        nn::os::LockMutex(&g_lock);
        for (const auto& saved : g_visibilitySlots)
            owned |= saved.active && owner == saved.unit;
        nn::os::UnlockMutex(&g_lock);
        if (c.active) {
            for (unsigned i = 0; i < c.glowCount; ++i)
                owned |= owner == c.glowUnits[i];
            for (unsigned i = 0; i < c.hiddenCount; ++i)
                owned |= owner == c.hiddenUnits[i];
        }
    } else {
        owned = c.hideGlider && c.playerSounds && read<void*>(owner, 0x20) == c.playerSounds;
    }
    // The replayed CMP sets flags and sends owned objects through the native virtual call.
    if (owned)
        gp->X[9] = 0;
}
HOOK_DEFINE_INLINE(RootWorldDispatchHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) { routeNativeDispatch(ctx, 0); }
};
HOOK_DEFINE_INLINE(WorldDispatchHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) { routeNativeDispatch(ctx, 1); }
};
HOOK_DEFINE_INLINE(BeforeDrawDispatchHook) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) { routeNativeDispatch(ctx, 2); }
};
HOOK_DEFINE_INLINE(SoundDispatchHook1) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) { routeNativeDispatch(ctx, 3); }
};
HOOK_DEFINE_INLINE(SoundDispatchHook2) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) { routeNativeDispatch(ctx, 4); }
};
HOOK_DEFINE_INLINE(SoundDispatchHook3) {
    static void Callback(exl::hook::InlineFloatCtx* ctx) { routeNativeDispatch(ctx, 5); }
};
void restoreEffect(void* executor) {
    auto* emitter = read<void*>(executor, 0xB8);
    const auto id = read<std::uint32_t>(executor, 0xC0);
    nn::os::LockMutex(&g_lock);
    const auto saved = g_effectMasks.restore({reinterpret_cast<std::uintptr_t>(executor),
                                              reinterpret_cast<std::uintptr_t>(emitter), id});
    if (saved && emitter && read<std::uint32_t>(emitter, 0x234) == id)
        std::memcpy(static_cast<std::byte*>(emitter) + 0x34, &*saved, 4);
    nn::os::UnlockMutex(&g_lock);
}
HOOK_DEFINE_TRAMPOLINE(EffectCalcHook) {
    static std::uint64_t Callback(void* executor) {
        restoreEffect(executor);
        const auto result = Orig(executor);
        Control c{};
        auto* user = read<void*>(executor, 0x20);
        if (!user || !snapshot(c) || !c.poseArm ||
            (user != c.weaponEffects[0] && user != c.weaponEffects[1]))
            return result;
        auto* emitter = read<void*>(executor, 0xB8);
        const auto id = read<std::uint32_t>(executor, 0xC0);
        if (!emitter || read<std::uint32_t>(emitter, 0x234) != id)
            return result;
        nn::os::LockMutex(&g_lock);
        if (g_effectMasks.remember({reinterpret_cast<std::uintptr_t>(executor),
                                    reinterpret_cast<std::uintptr_t>(emitter), id},
                                   read<unsigned>(emitter, 0x34))) {
            const unsigned hidden = 0;
            std::memcpy(static_cast<std::byte*>(emitter) + 0x34, &hidden, 4);
        }
        nn::os::UnlockMutex(&g_lock);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(EffectDestroyedHook) {
    static std::uint64_t Callback(void* executor) {
        restoreEffect(executor);
        return Orig(executor);
    }
};
struct GliderSoundVolume {
    void* executor{};
    float saved{};
    explicit GliderSoundVolume(void* sound) {
        Control c{};
        if (!snapshot(c) || !c.hideGlider || !c.playerSounds ||
            read<void*>(sound, 0x20) != c.playerSounds)
            return;
        const auto* key = read<const char*>(read<void*>(sound, 0x28), 0);
        if (!key || !pose::gliderSound(key))
            return;
        saved = read<float>(sound, 0x38);
        if (!std::isfinite(saved))
            return;
        executor = sound;
        const float zero = 0;
        std::memcpy(static_cast<std::byte*>(executor) + 0x38, &zero, 4);
    }
    ~GliderSoundVolume() {
        if (!executor)
            return;
        // Mark restored sound volume dirty so the next native update recalculates it.
        std::memcpy(static_cast<std::byte*>(executor) + 0x38, &saved, 4);
        const auto dirty = static_cast<std::uint16_t>(read<std::uint16_t>(executor, 0x82) | 1);
        std::memcpy(static_cast<std::byte*>(executor) + 0x82, &dirty, 2);
    }
};
HOOK_DEFINE_TRAMPOLINE(SoundCalcHook) {
    static std::uint64_t Callback(void* executor) {
        GliderSoundVolume volume(executor);
        return Orig(executor);
    }
};
HOOK_DEFINE_TRAMPOLINE(SoundEmitHook) {
    static std::uint64_t Callback(void* executor) {
        GliderSoundVolume volume(executor);
        return Orig(executor);
    }
};
HOOK_DEFINE_TRAMPOLINE(ClimbEnterHook) {
    static std::uint64_t Callback(void* action) {
        auto* player = native<void* (*)(void*)>(g_profile->functions.actionPlayer.offset)(action);
        g_climbingPlayer.store(reinterpret_cast<std::uintptr_t>(player), std::memory_order_release);
        return Orig(action);
    }
};
HOOK_DEFINE_TRAMPOLINE(ClimbLeaveHook) {
    static std::uint64_t Callback(void* action) {
        auto* player = native<void* (*)(void*)>(g_profile->functions.actionPlayer.offset)(action);
        auto expected = reinterpret_cast<std::uintptr_t>(player);
        g_climbingPlayer.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
        return Orig(action);
    }
};

void bindGlideAnimation(Control& c, const HookshotRuntime& rt) {
    if (c.active && c.glideBlend.weight > 0 && !rt.positionDrive.failed) {
        auto* board = read<void*>(c.animation, 0x40);
        const auto setter = read<std::uintptr_t>(read<void*>(board, 0), 0x70);
        if (read<void*>(board, 0xD0) &&
            setter == g_base + g_profile->functions.floatSetter.offset) {
            const char* magnitude = "LeftStickLength";
            const char* direction = "RadDiffDirAndLeftStick";
            const auto find = native<int (*)(void*, const char* const*)>(
                g_profile->functions.blackboardIndex.offset);
            const int m = find(board, &magnitude), d = find(board, &direction);
            const unsigned count = read<unsigned>(read<void*>(board, 0x20), 0);
            if (m >= 0 && d >= 0 && m != d && unsigned(m) < count && unsigned(d) < count &&
                unsigned(m) == read<unsigned>(c.player, 0xCF0) &&
                unsigned(d) == read<unsigned>(c.player, 0xD14)) {
                c.glideBoard = board;
                c.glideMagnitude = unsigned(m);
                c.glideDirection = unsigned(d);
            } else
                reject(17, (unsigned(m) & 0xFFFF) | (unsigned(d) << 16));
        } else
            reject(18, unsigned(setter - g_base));
    }
}

void updatePoseControl(Control& c, const HookshotRuntime& rt) {
    const bool climbing = c.player && g_climbingPlayer.load(std::memory_order_acquire) ==
                                          reinterpret_cast<std::uintptr_t>(c.player);
    const auto mode = pose::presentation(
        rt.machine.phase, rt.drive.parasailActive.load(std::memory_order_acquire) != 0, climbing);
    c.active = c.body && c.requested;
    c.poseArm = c.active && mode.rightArm;
    c.lowerLeftArm = c.active && mode.leftArm;
    c.trackHand = c.body && mode.track;
    c.glideAim = c.active && mode.glideSteering;
    if (c.requested && !c.active)
        reject(11);
    c.hideGlider = mode.gliderHidden;
    float cameraRotation[9]{};
    if (world::readCameraRotation(cameraRotation))
        c.cameraForward = {-cameraRotation[2], 0, -cameraRotation[8]};
    float playerRotation[9]{};
    if (world::readPlayerRotation(playerRotation))
        c.forward = {playerRotation[2], 0, playerRotation[8]};
    else
        c.active = false;
    c.aiming = rt.machine.phase == Phase::Targeting || rt.machine.phase == Phase::Confirming;
    if (rt.machine.phase == Phase::Targeting || rt.machine.phase == Phase::Confirming) {
        c.target = rt.aim.sample.position;
        if (!rt.aim.sample.hit) {
            float rotation[9]{};
            if (world::readCameraRotation(rotation))
                c.target = add(world::cameraPosition(),
                               mul(Vec3{-rotation[2], -rotation[5], -rotation[8]}, 30));
            else
                c.active = false;
        }
    } else
        c.target = rt.launch.anchor;
    if (!finite3(c.target))
        c.active = false;
    c.poseArm &= c.active;
    c.lowerLeftArm &= c.active;
    c.glideAim &= c.active;
    c.glideBlend = travelGlideBlend(rt.machine.phase,
                                    rt.drive.parasailActive.load(std::memory_order_acquire) != 0,
                                    climbing, rt.positionDrive.path, c.forward);
    bindGlideAnimation(c, rt);
}

void collectEquipment(Control& c, void* player, void* equipment) {
    if (c.requested || c.alignArrowModels)
        addGlowUnits(c, modelRoot(player));
    if (equipment) {
        // EquipmentUser holds eight dynamic and twelve static links; release every borrowed reference.
        if (c.requested || c.alignArrowModels)
            for (unsigned i = 0; i < 20; ++i) {
                auto part =
                    native<Reference (*)(const void*)>(g_profile->functions.getReference.offset)(
                        static_cast<std::byte*>(equipment) + 0x20 + i * 0x18);
                addGlowUnits(c, modelRoot(part.actor));
            }
        auto sword = native<Reference (*)(const void*)>(g_profile->functions.getReference.offset)(
            static_cast<std::byte*>(equipment) + 0x20);
        c.weaponEffects[0] = effectUser(sword.actor);
        addHiddenUnits(c, modelRoot(sword.actor), false);
        auto* weapon = read<void*>(components(sword.actor), 0x208);
        if (weapon && read<std::uint8_t>(weapon, 0x50C)) {
            auto fuse = native<Reference (*)(const void*)>(
                g_profile->functions.getReference.offset)(static_cast<std::byte*>(weapon) + 0xB0);
            c.weaponEffects[1] = effectUser(fuse.actor);
            addHiddenUnits(c, modelRoot(fuse.actor), false);
        }
    }
    if (c.player) {
        auto glider = native<Reference (*)(const void*)>(g_profile->functions.getReference.offset)(
            static_cast<std::byte*>(c.player) + 0x6B8);
        addHiddenUnits(c, modelRoot(glider.actor), true);
    }
}
}

void install(std::uintptr_t mainBase) {
    const auto* game = arrowbound::profiles::active();
    if (g_installed || !game)
        return;
    g_profile = profiles::find(game->version);
    if (!g_profile) {
        Logging.Log("[zonai-hookshot] PLAYER_PRESENTATION unavailable version=%s", game->name);
        return;
    }
    const auto& f = g_profile->functions;
    const arrowbound::profiles::Site guards[]{
        f.playerAnimation, f.modelWorld,         f.shapeVisible,      f.modelBeforeDraw,
        f.effectCalc,      f.effectDestroyed,    f.soundCalc,         f.soundEmit,
        f.groundWait,      f.groundLockOnWait,   f.groundMove,        f.groundLockOnMove,
        f.climbEnter,      f.climbLeave,         f.floatSetter,       f.bodyMerge,
        f.animatedPose,    f.releaseReference,   f.currentCommand,    f.xlinkComponent,
        f.boneIndex,       f.boneWorld,          f.boneLocal,         f.setBoneLocal,
        f.setBoneWorld,    f.setMaterialVisible, f.lowerCommand,      f.playerYaw,
        f.turnVelocity,    f.setAngularVelocity, f.actionPlayer,      f.rightArmCommand,
        f.parameterIndex,  f.parameterFloat,     f.setParameterFloat, f.parameterSrt,
        f.setParameterSrt, f.materialName,       f.blackboardIndex,   f.getReference,
        f.bodyMatrix,
    };
    for (const auto& site : guards)
        if (!arrowbound::profiles::entryHookable(mainBase, site, "player presentation"))
            return;
    for (const auto& route : g_profile->routes)
        if (route.site.offset &&
            !arrowbound::profiles::entryHookable(mainBase, route.site, "presentation dispatch"))
            return;
    constexpr arrowbound::profiles::Site cameraTarget{0x84578C, 0xD10683FF};
    const bool arrowCamera = game->version == arrowbound::profiles::GameVersion::V121;
    constexpr arrowbound::profiles::Site arrowSceneSites[]{
        {0x974D9C, 0xA9BA7BFD}, {0x832640, 0xD101C3FF},
        {0x64D388, 0xD10343FF}, {0x2216CC4, 0xA9BE7BFD},
    };
    if (arrowCamera) {
        if (!arrowbound::profiles::entryHookable(mainBase, cameraTarget, "arrow camera target")) return;
        for (const auto& site : arrowSceneSites)
            if (!arrowbound::profiles::entryHookable(mainBase, site, "arrow scene pose")) return;
    }
    g_base = mainBase;
    nn::os::InitializeMutex(&g_lock, true, 0);
    g_installed = true;
    PlayerAnimationHook::InstallAtOffset(f.playerAnimation.offset);
    ModelWorldHook::InstallAtOffset(f.modelWorld.offset);
    ShapeVisibleHook::InstallAtOffset(f.shapeVisible.offset);
    ModelBeforeDrawHook::InstallAtOffset(f.modelBeforeDraw.offset);
    if (arrowCamera) {
        ArrowCameraTargetHook::InstallAtOffset(cameraTarget.offset);
        ArrowSceneFrameHook::InstallAtOffset(arrowSceneSites[0].offset);
    }
    EffectCalcHook::InstallAtOffset(f.effectCalc.offset);
    EffectDestroyedHook::InstallAtOffset(f.effectDestroyed.offset);
    SoundCalcHook::InstallAtOffset(f.soundCalc.offset);
    SoundEmitHook::InstallAtOffset(f.soundEmit.offset);
    GroundWaitHook::InstallAtOffset(f.groundWait.offset);
    GroundLockOnWaitHook::InstallAtOffset(f.groundLockOnWait.offset);
    GroundMoveHook::InstallAtOffset(f.groundMove.offset);
    GroundLockOnMoveHook::InstallAtOffset(f.groundLockOnMove.offset);
    ClimbEnterHook::InstallAtOffset(f.climbEnter.offset);
    ClimbLeaveHook::InstallAtOffset(f.climbLeave.offset);
    GlideAnimationFloatHook::InstallAtOffset(f.floatSetter.offset);
    TravelBodyAnchorHook::InstallAtOffset(f.bodyMerge.offset);
    if (!g_profile->nativeTranslation)
        BodyMatrixHook::InstallAtOffset(f.bodyMatrix.offset);
    if (g_profile->routes[0].site.offset) RootWorldDispatchHook::InstallAtOffset(g_profile->routes[0].site.offset);
    if (g_profile->routes[1].site.offset) WorldDispatchHook::InstallAtOffset(g_profile->routes[1].site.offset);
    if (g_profile->routes[2].site.offset) BeforeDrawDispatchHook::InstallAtOffset(g_profile->routes[2].site.offset);
    if (g_profile->routes[3].site.offset) SoundDispatchHook1::InstallAtOffset(g_profile->routes[3].site.offset);
    if (g_profile->routes[4].site.offset) SoundDispatchHook2::InstallAtOffset(g_profile->routes[4].site.offset);
    if (g_profile->routes[5].site.offset) SoundDispatchHook3::InstallAtOffset(g_profile->routes[5].site.offset);
    Logging.Log("[zonai-hookshot] PLAYER_PRESENTATION installed version=%s", game->name);
}

void reset() {
    if (!g_installed)
        return;
    g_climbingPlayer.store(0, std::memory_order_release);
    g_glideBoard.store(nullptr, std::memory_order_release);
    nn::os::LockMutex(&g_lock);
    g_control.anchorTravel = false;
    g_control.active = false;
    g_control.hideGlider = false;
    g_control.trackHand = false;
    g_control.poseArm = false;
    g_control.glideAim = false;
    g_control.glideBoard = nullptr;
    g_control.glideBlend = {};
    g_result = {};
    nn::os::UnlockMutex(&g_lock);
}
void update(const HookshotRuntime& rt) {
    if (!g_installed)
        return;
    Control c{};
    c.generation = rt.session.worldGen;
    c.anchorTravel = rt.machine.phase == Phase::PositionCruise && rt.positionDrive.path.active &&
                     !rt.positionDrive.failed;
    const auto& arrow = arrowbound::runtime();
    if (!c.anchorTravel && arrowbound::profiles::active()->version ==
                               arrowbound::profiles::GameVersion::V121) {
        const auto flight = arrowFlightPresentation(
            arrow.arrowTrip.phase, arrow.drive.parasailActive.load(std::memory_order_acquire) != 0,
            g_climbingPlayer.load(std::memory_order_acquire) != 0, arrow.arrowTrip.arrowVelocity);
        c.arrowToken = arrow.arrow.controllerToken.load(std::memory_order_acquire);
        c.arrowShot = arrow.arrowTrip.shotSeqSeen;
        c.anchorTravel = flight.anchor && c.arrowToken != 0;
        c.arrowAnimationWeight = flight.animationWeight;
        c.alignArrowModels = flight.alignModels && c.anchorTravel &&
                             arrow.arrowTrip.haveRequestedPosition;
    }
    c.requested = pose::ownsArm(rt.machine.phase);
    c.trackHand = pose::tracksHand(rt.machine.phase);
    c.aiming = pose::isAiming(rt.machine.phase);
    c.npad = rt.walk.targetNpadId.load(std::memory_order_relaxed);
    c.npadValid = rt.walk.targetNpadValid.load(std::memory_order_acquire) != 0;
    if (world::ready()) {
        auto* player = world::playerActor();
        auto* registry = components(player);
        c.position = world::playerPosition();
        c.scene = world::state().sceneToken.value;
        c.actor = reinterpret_cast<std::uintptr_t>(player);
        c.player = read<void*>(registry, 0x3A8);
        c.playerSounds = read<void*>(
            native<void* (*)(void*)>(g_profile->functions.xlinkComponent.offset)(player), 0x78);
        c.animation = animation(player);
        auto* equipment = read<void*>(registry, 0x230);
        c.body = firstUnit(modelRoot(player));
        c.bodySkeleton = read<void*>(c.body, g_profile->model.skeleton);
        if (c.anchorTravel) {
            const auto* profile = arrowbound::profiles::active();
            auto* set = native<void* (*)(void*)>(profile->physics.getControllerSet.offset)(player);
            if (set)
                c.ragdollStructure = native<void* (*)(void*, unsigned)>(
                    profile->physics.getStructure.offset)(set, 0);
            if (!c.ragdollStructure)
                reject(22);
        }
        collectEquipment(c, player, equipment);
        updatePoseControl(c, rt);
    } else {
        c.trackHand = false;
    }
    static std::uint64_t glowStart{};
    static bool glowing{};
    if (c.active && !glowing)
        glowStart = svcGetSystemTick();
    glowing = c.active;
    const auto frequency = nn::os::GetSystemTickFrequency();
    if (glowing && frequency)
        c.glowFrame = static_cast<float>((svcGetSystemTick() - glowStart) % (frequency * 16)) /
                      static_cast<float>(frequency) * 30.f;
    nn::os::LockMutex(&g_lock);
    if (!c.trackHand || c.generation != g_control.generation || c.body != g_control.body)
        g_result = {};
    // Retain an identity token solely so the old model's own callback can restore its visuals.
    if (!c.actor) {
        c.scene = g_control.scene;
        c.actor = g_control.actor;
    }
    g_control = c;
    nn::os::UnlockMutex(&g_lock);
    g_glideMagnitude.store(c.glideMagnitude, std::memory_order_relaxed);
    g_glideDirection.store(c.glideDirection, std::memory_order_relaxed);
    g_glideBoard.store(c.glideBoard, std::memory_order_release);
}
void steerGlide(void* controller) {
    Control c{};
    if (!snapshot(c) || !c.glideAim || !c.npadValid)
        return;
    input::ProcessedController processed{};
    if (!input::openProcessedController(controller, processed) || processed.npadId != c.npad ||
        !processed.samplingNumber)
        return;
    pose::AimCone cone{};
    float x{}, y{};
    if (!pose::constrainAim(c.forward, sub(c.target, c.position), cone) ||
        !pose::glideStick(c.cameraForward, sub(c.target, c.position), x, y))
        return;
    // Native Parasail consumes camera-relative Npad steering, preserving its own turn animation.
    if (std::fabs(cone.yaw) > 0.13962634f) {
        processed.leftStick[0] = x;
        processed.leftStick[1] = y;
    }
}
void observeClimb(void* action) {
    if (!g_installed)
        return;
    auto* player = native<void* (*)(void*)>(g_profile->functions.actionPlayer.offset)(action);
    g_climbingPlayer.store(reinterpret_cast<std::uintptr_t>(player), std::memory_order_release);
}
bool handOrigin(Vec3& out) {
    if (!g_installed)
        return false;
    nn::os::LockMutex(&g_lock);
    const auto frequency = nn::os::GetSystemTickFrequency(), now = svcGetSystemTick();
    const bool ok = g_control.trackHand && g_result.ready &&
                    g_result.generation == g_control.generation && frequency &&
                    now >= g_result.time && now - g_result.time <= frequency / 4;
    if (ok)
        out = g_result.hand;
    nn::os::UnlockMutex(&g_lock);
    return ok;
}
bool usesHandOrigin() { return g_installed; }
}
