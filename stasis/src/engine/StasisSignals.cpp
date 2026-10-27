// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "StasisSignals.hpp"
#include "StasisActors.hpp"
#include "StasisCamera.hpp"
#include "StasisHit.hpp"
#include "StasisInput.hpp"
#include "Totk121Signatures.hpp"
#include "totk/engine/MotionCamera.hpp"
#include <lib.hpp>
#include <cstring>
namespace stasis::engine {
std::uintptr_t moduleBase{};
Signals signals;
namespace {
namespace eng = totk::engine;
camera::Blend g_cameraBlend;
eng::motion::AimState g_motion;
// Publishes change(wheel) for the screen that owns the wheel; false if change declines or loses.
template <class Change>
bool updateWheel(std::uintptr_t screen, input::Wheel &wheel, Change change) {
    if (signals.wheelOwner.load(std::memory_order_acquire) != screen)
        return false;
    auto prior = signals.wheel.load(std::memory_order_acquire);
    wheel = input::unpack(prior);
    return change(wheel) && signals.wheel.compare_exchange_strong(prior, input::pack(wheel),
                                                                  std::memory_order_acq_rel);
}
// True the first time a refusal bit is set during a hold.
bool firstRefusal(unsigned bit) { return !(signals.motionRefused.fetch_or(bit) & bit); }
} // namespace

bool validate(std::uintptr_t base) {
    eng::motion::validate(base);
    return totk::engine::codeMatches(base, signatures::kRuntime, "stasis", "BUILD_REFUSED");
}
void nativeSelectionAccepted(std::uintptr_t screen) {
    const auto count = readGame.template operator()<int>(screen + 0x1A8);
    const auto cursor = readGame.template operator()<int>(screen + 0x23C);
    if (count < 1 || count > 8 || cursor < 0 || cursor >= count)
        return;
    signals.recentOwned.store(false, std::memory_order_release);
    signals.nativeSelections.fetch_add(1, std::memory_order_release);
    Logging.Log("[stasis] NATIVE_SELECTION cursor=%d", cursor);
}
std::uint64_t nativeAbilityAdmission(std::uint64_t (*original)(std::uintptr_t), std::uintptr_t actor) {
    const input::RecentAbility recent{signals.recentOwned.load(std::memory_order_acquire)};
    if (!recent.ownsNative(actor, signals.recentPlayer.load(std::memory_order_acquire)))
        return original(actor);
    static std::atomic<unsigned> reports{};
    if (reports.fetch_add(1, std::memory_order_relaxed) < 8)
        Logging.Log("[stasis] NATIVE_ABILITY_SUPPRESSED player=%p", reinterpret_cast<void *>(actor));
    return 0;
}
void wheelOpened(std::uintptr_t screen) {
    const auto epoch = signals.wheelEpoch.fetch_add(1, std::memory_order_relaxed) + 1;
    signals.wheelOwner.store(screen, std::memory_order_release);
    signals.wheel.store(input::pack({epoch, true, input::cameraSelected(screen, readGame)}),
                        std::memory_order_release);
    Logging.Log("[stasis] WHEEL_OPEN epoch=%u", epoch);
}
void wheelUpdated(std::uintptr_t screen) {
    input::Wheel wheel;
    bool camera{};
    if (updateWheel(screen, wheel, [&](input::Wheel &w) {
            if (!w.open || w.exited)
                return false;
            camera = input::cameraSelected(screen, readGame);
            if (w.camera == camera)
                return false;
            w.camera = camera;
            return true;
        }))
        Logging.Log("[stasis] WHEEL_CAMERA epoch=%u selected=%u", wheel.epoch, unsigned(camera));
}
bool wheelChosen(std::uintptr_t screen) {
    input::Wheel wheel;
    const bool chosen = updateWheel(screen, wheel, [](input::Wheel &w) {
        if (!w.open || w.exited || w.chosen)
            return false;
        w.chosen = true;
        return true;
    });
    Logging.Log("[stasis] WHEEL_PAGE_CHOICE epoch=%u accepted=%u", wheel.epoch, unsigned(chosen));
    return chosen;
}
bool wheelCancelRequested(std::uintptr_t screen) {
    const auto wheel = input::unpack(signals.wheel.load(std::memory_order_acquire));
    return signals.wheelOwner.load(std::memory_order_acquire) == screen && wheel.open &&
           wheel.epoch && signals.wheelRequest.load(std::memory_order_acquire) == wheel.epoch;
}
void wheelExited(std::uintptr_t screen, bool cancelled) {
    input::Wheel wheel;
    if (updateWheel(screen, wheel, [&](input::Wheel &w) {
            if (!w.epoch || w.exited)
                return false;
            w.open = w.camera = false;
            w.exited = true;
            w.cancelled = cancelled;
            return true;
        }))
        Logging.Log("[stasis] WHEEL_EXIT epoch=%u cancelled=%u request=%u", wheel.epoch,
                    unsigned(cancelled), signals.wheelRequest.load());
}
void wheelClosed(std::uintptr_t screen) {
    input::Wheel wheel;
    if (updateWheel(screen, wheel, [](input::Wheel &w) {
            w.open = w.camera = false;
            w.closed = true;
            return true;
        }))
        Logging.Log("[stasis] WHEEL_CLOSED epoch=%u", wheel.epoch);
}
void publishPhysicsTime(float frameScale) {
    const auto system = read<std::uintptr_t>(moduleBase + offsets::kPhysicsSystem);
    const auto time = valid(system) ? read<std::uintptr_t>(system + 0xC8) : 0;
    const bool running = valid(time) && !(read<std::uint32_t>(time + 0x18) & 1) &&
                         std::isfinite(frameScale) && frameScale > 0.0f && frameScale < 100.0f;
    // Cumulative frames in 32.32 fixed point, so an overlapping input read loses no time.
    if (running)
        signals.clockTicks.fetch_add(
            static_cast<std::uint64_t>(double(frameScale) * 4294967296.0 + 0.5),
            std::memory_order_release);
    signals.clockRunning.store(running, std::memory_order_release);
}
void acceptedHit(void *record, void *victimComponent, void *attackInfo) {
    const auto target = signals.frozenActor.load(std::memory_order_acquire);
    if (!target || !record || !victimComponent || !attackInfo)
        return;
    // The hook passes the victim's component, not its actor reference.
    const auto victim =
        actorReference(hit_adapter::actorLink(reinterpret_cast<std::uintptr_t>(victimComponent)));
    if (victim.actor != target)
        return;
    const auto payload = hit_adapter::decode(reinterpret_cast<std::uintptr_t>(record), readGame);
    Hit hit;
    hit.victim = target;
    hit.normal = payload.normal;
    hit.point = payload.point;
    hit.direction = payload.normal;
    std::memcpy(hit.attack, payload.attack, sizeof(hit.attack));
    hit.generation = signals.generation.load(std::memory_order_acquire);
    const auto attacker =
        actorReference(hit_adapter::actorLink(reinterpret_cast<std::uintptr_t>(attackInfo)));
    hit.attacker = attacker.actor;
    const auto weapon = weaponOf(hit.attacker);
    hit.weapon = weapon.family;
    hit.playerStrike = startsWith(actorName(weapon.user), "Player") ||
                       startsWith(actorName(hit.attacker), "Player");
    if (hit.playerStrike) {
        if (const auto body = mainBody(target); !valid(body))
            Logging.Log("[stasis] PLAYER_DIRECTION_REFUSED missing body");
        else if (const auto com = centerOfMass(body), origin = position(weapon.user);
                 finite(com) && finite(origin))
            hit.direction = hit_adapter::playerDirection(hit.normal, origin, com);
        else
            Logging.Log("[stasis] PLAYER_DIRECTION_REFUSED invalid position/COM");
    } else if (valid(hit.attacker) &&
               call<bool (*)(std::uintptr_t, bool, bool)>(offsets::kGameActorIsProjectile)(
                   hit.attacker, false, false)) {
        // BotW's Bullet branch uses travel direction, retaining the incoming force magnitude.
        if (const auto body = mainBody(hit.attacker); valid(body)) {
            const auto velocity = call<std::uintptr_t (*)(std::uintptr_t)>(
                offsets::kRigidBodyEntityGetLinearVelocity)(body);
            if (valid(velocity))
                hit.direction = hit_adapter::projectileDirection(hit.normal, read<Vec3>(velocity));
        }
    }
    const bool queued = signals.hits.tryUpdate([&](HitQueue &queue) {
        if (queue.count < kHitCapacity)
            queue.hits[queue.count++] = hit;
        else
            ++signals.hitDrops;
    });
    if (!queued)
        ++signals.hitDrops;
}
bool pauseAiNode(std::uintptr_t node) {
    const auto target = signals.pausedActor.load(std::memory_order_acquire);
    if (!target || !valid(node))
        return false;
    // The AI element's context chain, which the game also uses to find a node's actor.
    const auto instance = read<std::uintptr_t>(node + 8);
    const auto context = valid(instance) ? read<std::uintptr_t>(instance + 8) : 0;
    return valid(context) && read<std::uintptr_t>(context + 504) == target;
}
bool pauseAnimation(std::uintptr_t controller) {
    const auto target = signals.pausedAnimation.load(std::memory_order_acquire);
    return target && controller == target;
}
bool pauseLife(std::uintptr_t component) {
    const auto held = signals.protectedObject.load(std::memory_order_acquire);
    if (!held || !valid(component) || read<std::uintptr_t>(component + 24) != held)
        return false;
    const auto anchor = signals.enemyAnchor.read();
    return matchesEnemyAnchor(anchor, held,
                              read<std::uintptr_t>(held + eng::layout::kActorNamePointer),
                              signals.frozenActor.load(std::memory_order_acquire),
                              signals.pausedActor.load(std::memory_order_acquire),
                              signals.generation.load(std::memory_order_acquire));
}
void holdEnemyPhysics(void *component) {
    const auto held = signals.pausedActor.load(std::memory_order_acquire);
    if (!held)
        return;
    const auto address = reinterpret_cast<std::uintptr_t>(component);
    if (!valid(address)) {
        if (firstRefusal(1))
            Logging.Log("[stasis] ENEMY_MOTION_REFUSED physics=%p", component);
        return;
    }
    stasis::holdEnemyPhysics(
        held, address, [](std::uintptr_t field) { return read<std::uintptr_t>(field); },
        [](std::uintptr_t actor) {
            // Runs after the game's movement and impulse requests, even when the character
            // update skips applying velocity.
            const auto collection = bodies(controllerSet(actor));
            if (collection.overflow || !collection.count) {
                if (firstRefusal(2))
                    Logging.Log("[stasis] ENEMY_MOTION_REFUSED body_count=%u overflow=%u",
                                collection.count, unsigned(collection.overflow));
                return false;
            }
            for (unsigned i = 0; i < collection.count; ++i) {
                const auto flags = read<std::uint64_t>(collection.address[i] + 104);
                if ((flags & 0xC0000000ull) && firstRefusal(4))
                    Logging.Log("[stasis] ENEMY_MOTION_REFUSED velocity_write_flags=%llx body=%p",
                                flags, reinterpret_cast<void *>(collection.address[i]));
                stopBody(collection.address[i]);
            }
            return true;
        });
}
std::uintptr_t updateEnemyPosition(UpdatePosition original, std::uintptr_t actor, float *matrix,
                                   float frameScale) {
    if (actor != signals.pausedActor.load(std::memory_order_acquire) || !actor)
        return original(actor, matrix, frameScale);
    const auto anchor = signals.enemyAnchor.read();
    const auto active = [&] {
        return matchesEnemyAnchor(anchor, actor,
                                  read<std::uintptr_t>(actor + eng::layout::kActorNamePointer),
                                  signals.frozenActor.load(std::memory_order_acquire),
                                  signals.pausedActor.load(std::memory_order_acquire),
                                  signals.generation.load(std::memory_order_acquire));
    };
    if (!valid(reinterpret_cast<std::uintptr_t>(matrix)) || !std::isfinite(frameScale) ||
        frameScale <= 0) {
        if (firstRefusal(8))
            Logging.Log("[stasis] ENEMY_POSITION_REFUSED actor=%p matrix=%p frame_scale=%f",
                        reinterpret_cast<void *>(actor), matrix, double(frameScale));
        return original(actor, matrix, frameScale);
    }
    return updateAnchoredEnemy(
        anchor.matrix, matrix, [&] { return original(actor, matrix, frameScale); },
        [&](const float *pose) { forcePose(actor, pose); },
        [&](const float *pose) { commitPose(actor, pose, frameScale); }, active);
}
void observeRaycast(target::Cast original, const void *from, const void *object) {
    signals.target.observe(original, from, object, resolveHitBody);
}
void frameAimCamera(std::uintptr_t state, float *output, float frameScale) {
    CameraRequest request;
    if (!signals.cameraRequest.tryRead(request))
        return;
    if (request.session != signals.cameraSession.load(std::memory_order_acquire) ||
        !request.player || !request.scene) {
        g_cameraBlend.reset();
        g_motion = {};
        return;
    }
    if (!request.aiming && g_cameraBlend.amount == 0) {
        g_motion = {};
        return;
    }
    // The player camera's resolve state sits 1096 bytes into its component, whose target
    // record at +3144 holds the followed actor at +24.
    const auto component = state - 1096;
    const auto target = valid(component) ? read<std::uintptr_t>(component + 3144) : 0;
    const auto actor = valid(target) ? read<std::uintptr_t>(target + 24) : 0;
    if (actor != request.player) {
        static unsigned refusals{};
        if (request.aiming && (++refusals <= 3 || refusals % 300 == 0))
            Logging.Log("[stasis] AIM_CAMERA_REFUSED target=%p player=%p state=%p count=%u",
                        reinterpret_cast<void *>(actor), reinterpret_cast<void *>(request.player),
                        reinterpret_cast<void *>(state), refusals);
        if (g_cameraBlend.owner == state)
            g_cameraBlend.reset();
        g_motion = {};
        return;
    }
    camera::View view{{output[0], output[1], output[2]},
                      {output[3], output[4], output[5]},
                      {output[6], output[7], output[8]},
                      output[9]};
    const auto before = view;
    const float previous = g_cameraBlend.amount;
    const float frames = signals.clockRunning.load(std::memory_order_acquire) ? frameScale : 0.0f;
    if (!g_cameraBlend.apply(view, state, request.session, request.aiming, frames)) {
        g_motion = {};
        static unsigned refusals{};
        if (++refusals <= 3 || refusals % 300 == 0)
            Logging.Log("[stasis] AIM_CAMERA_REFUSED distance=%f fovy=%f frames=%f count=%u",
                        double(length(sub(before.at, before.eye))), double(before.fovy),
                        double(frameScale), refusals);
        return;
    }
    std::memcpy(output, &view, sizeof(view));
    eng::motion::applyCamera(g_motion,
        {state, request.session, signals.clockTicks.load(std::memory_order_acquire), request.aiming,
         signals.clockRunning.load(std::memory_order_acquire),
         input::unpack(signals.wheel.load(std::memory_order_acquire)).open}, output,
        read<eng::motion::Vector>(request.player + eng::layout::kActorPosition));
    if ((previous < 1 && g_cameraBlend.amount == 1) || (previous > 0 && g_cameraBlend.amount == 0))
        Logging.Log("[stasis] AIM_CAMERA amount=%f shift=%f,%f,%f fovy=%f",
                    double(g_cameraBlend.amount), double(view.eye.x - before.eye.x),
                    double(view.eye.y - before.eye.y), double(view.eye.z - before.eye.z),
                    double(view.fovy));
}
} // namespace stasis::engine
