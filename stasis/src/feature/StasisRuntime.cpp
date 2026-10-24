// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "StasisRuntime.hpp"
#include "StasisActors.hpp"
#include "StasisAudio.hpp"
#include "StasisInput.hpp"
#include "StasisSignals.hpp"
#include "totk/engine/AbilityChoice.hpp"
#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/Npad.hpp"
#include <lib.hpp>
#include <cstring>

namespace stasis::runtime {
using namespace engine;
namespace {
namespace eng = totk::engine;
constexpr Parameters kParameters{};
struct Clock {
    double elapsedFrames{};
    bool running{};
};
struct HitKey {
    std::uint32_t attack[4]{};
    std::uintptr_t attacker{};
};
struct State {
    Handle player, selected, frozen, released;
    eng::SceneContext scene;
    Handle roster[kRosterCapacity]{};
    std::size_t rosterCount{};
    Trail trail{};
    Vec3 trailOffset{}, lastPosition{};
    float trailRadius{};
    ChargeState charge;
    Bodies held{};
    Bounds frozenBounds{};
    HitKey keys[32]{};
    unsigned keyCount{}, keyCursor{}, strikes{}, countdownPhase{};
    float remaining{}, duration{}, cooldown{}, scalar{}, hitFlash{}, releaseFlash{};
    double previousClock{};
    std::uint64_t previousButtons{};
    std::uint32_t generation{1};
    bool aiming{}, enemy{}, ready{};
    unsigned tick{}, scanStarted{}, selectionAt{};
} g;
eng::NpadReader g_npad;
eng::SceneTracker g_scenes;
input::Activation g_activation;
input::RecentAbility g_recent;
eng::AbilityChoiceWatch g_otherAbilities;
unsigned g_seenSelections{};

const Handle *inRoster(std::uintptr_t address, std::uintptr_t name) {
    for (std::size_t i = 0; i < g.rosterCount; ++i)
        if (g.roster[i].address == address && g.roster[i].capturedNamePointer == name)
            return &g.roster[i];
    return nullptr;
}
bool seen(Handle handle) {
    return handle.address && handle.isCurrent(g.scene.token) &&
           inRoster(handle.address, handle.capturedNamePointer);
}
void setAiming(bool aiming) {
    g.aiming = aiming;
    g.selected = {};
    signals.target.cancel();
}
void clearHoldSignals() {
    signals.protectedObject.store(0, std::memory_order_release);
    signals.frozenActor.store(0, std::memory_order_release);
    signals.pausedActor.store(0, std::memory_order_release);
    signals.pausedAnimation.store(0, std::memory_order_release);
}
// Keeps the object under the reticle selected; a raycast on the physics worker finds it.
void selectTarget() {
    Ray ray;
    signals.cameraRay.tryRead(ray);
    const auto underReticle = [&](Handle actor) {
        Bounds bounds;
        return seen(actor) && worldBounds(actor.address,bounds) &&
            target::pointsAt(ray,bounds.min,bounds.max);
    };
    if (g.tick - g.selectionAt > 8 || !underReticle(g.selected))
        g.selected = {};
    target::Hit hit;
    if (signals.target.poll(hit, g.generation)) {
        g.selected = {};
        if (!hit.filterValid && g.tick % 60 < 2)
            Logging.Log("[stasis] TARGET_REFUSED native donor filter layout");
        const auto *candidate = hit.hit && hit.actor
                                    ? inRoster(hit.actor, hit.name)
                                    : nullptr;
        if (candidate && underReticle(*candidate) && eligible(*candidate)) {
            g.selected = *candidate;
            g.selectionAt = g.tick;
        }
    }
    if (signals.target.busy() && g.tick - g.scanStarted > 12) {
        signals.target.cancel();
        g.selected = {};
    }
    if (!signals.target.busy() && ray.valid) {
        const auto exclude = collisionGroup(g.player.address);
        if (signals.target.begin(ray, position(g.player.address), g.generation, exclude)) {
            g.scanStarted = g.tick;
        } else if (!exclude.enabled && g.tick % 120 == 0)
            Logging.Log("[stasis] TARGET_REFUSED missing player collision group");
    }
}
void resetSession(const char *reason) {
    g_recent.reset();
    signals.recentOwned.store(false, std::memory_order_release);
    signals.recentPlayer.store(0, std::memory_order_release);
    signals.cameraSession.fetch_add(1, std::memory_order_acq_rel);
    signals.cameraRequest.tryWrite({});
    g_activation.reset();
    signals.wheelRequest.store(0, std::memory_order_release);
    signals.wheelOwner.store(0, std::memory_order_release);
    signals.wheel.store(0, std::memory_order_release);
    signals.target.cancel();
    signals.cameraRay.tryWrite({});
    clearHoldSignals();
    signals.generation.store(++g.generation, std::memory_order_release);
    g.held = {};
    g.frozen = g.selected = g.released = {};
    g.charge = {};
    g.aiming = false;
    g.remaining = g.cooldown = g.hitFlash = g.releaseFlash = 0;
    g.trail.reset();
    signals.presentation.tryWrite({});
    Logging.Log("[stasis] SESSION_RESET reason=%s generation=%u", reason, g.generation);
}
void activate() {
    if (!input::canFreeze(g.frozen.address != 0, g.cooldown)) {
        Logging.Log("[stasis] ACTIVATE_REFUSED cooldown=%f frozen=%u", double(g.cooldown),
                    unsigned(g.frozen.address != 0));
        return;
    }
    if (!seen(g.selected) || !eligible(g.selected)) {
        Logging.Log("[stasis] ACTIVATE_REFUSED target=%p roster=%u",
                    reinterpret_cast<void *>(g.selected.address), unsigned(g.rosterCount));
        return;
    }
    const auto set = controllerSet(g.selected.address);
    const auto body = mainBody(g.selected.address);
    const auto structure = physicsStructure(set);
    if (!valid(body) || !valid(structure)) {
        Logging.Log("[stasis] ACTIVATE_REFUSED body=%p structure=%p",
                    reinterpret_cast<void *>(body), reinterpret_cast<void *>(structure));
        return;
    }
    const auto collection = bodies(set);
    if (collection.overflow || !collection.count ||
        !worldBounds(g.selected.address, g.frozenBounds)) {
        Logging.Log("[stasis] ACTIVATE_REFUSED incomplete body collection count=%u overflow=%u",
                    collection.count, unsigned(collection.overflow));
        return;
    }
    if (!holdAvailable(structure))
        return;
    const bool enemy = isLivingTarget(g.selected.address);
    EnemyAnchor anchor{g.selected.address, g.selected.capturedNamePointer, g.generation + 1};
    if (!readPose(anchor.actor, anchor.matrix)) {
        Logging.Log("[stasis] ACTIVATE_REFUSED nonfinite anchor actor=%p",
                    reinterpret_cast<void *>(anchor.actor));
        return;
    }
    g.frozen = g.selected;
    g.lastPosition = centre(g.frozenBounds);
    g.releaseFlash = 0;
    g.released = {};
    g.trail.reset();
    g.scalar = mass(body);
    g.enemy = enemy;
    g.duration = enemy ? kEnemyHoldFrames : kParameters.longFrames;
    g.remaining = g.duration;
    g.countdownPhase = 0;
    g.charge = {};
    g.keyCount = g.keyCursor = g.strikes = 0;
    g.aiming = false;
    signals.motionRefused = 0;
    // Objects can also receive movement/rotation requests after the initial keyframe change.
    // Use the same pose, animation and AI hold for every target until release.
    signals.enemyAnchor.write(anchor);
    signals.pausedAnimation.store(animationController(g.frozen.address),
                                  std::memory_order_release);
    signals.pausedActor.store(g.frozen.address, std::memory_order_release);
    signals.generation.store(++g.generation, std::memory_order_release);
    signals.target.cancel();
    g.held = collection;
    freeze(set);
    unsigned keyframed = 0;
    for (unsigned i = 0; i < g.held.count; ++i) {
        if (g.held.mode[i] == kDynamic) {
            setMotionType(g.held.address[i], kKeyframed);
            ++keyframed;
        }
        if (g.held.mode[i] == kDynamic || enemy)
            stopBody(g.held.address[i]);
    }
    signals.frozenActor.store(g.frozen.address, std::memory_order_release);
    signals.protectedObject.store(enemy ? 0 : g.frozen.address, std::memory_order_release);
    audio::play(audio::Cue::Start);
    Logging.Log("[stasis] FREEZE actor=%s actor_ptr=%p body=%p mass=%f frames=%f enemy=%u "
                "bodies=%u keyframed=%u",
                actorName(g.frozen.address), reinterpret_cast<void *>(g.frozen.address),
                reinterpret_cast<void *>(body), double(g.scalar), double(g.duration),
                unsigned(enemy), g.held.count, keyframed);
}
void release(const char *reason) {
    clearHoldSignals();
    if (!seen(g.frozen)) {
        resetSession("frozen actor lost");
        return;
    }
    g.lastPosition = centre(g.frozenBounds);
    g.releaseFlash = 1.0f;
    const auto set = controllerSet(g.frozen.address);
    const auto body = mainBody(g.frozen.address);
    const auto current = bodies(set);
    if (valid(set))
        unfreeze(set);
    // Restore the motion type of each held body that still belongs to the object.
    for (unsigned i = 0; i < g.held.count; ++i)
        for (unsigned j = 0; j < current.count; ++j)
            if (g.held.address[i] == current.address[j]) {
                setMotionType(g.held.address[i], g.held.mode[i]);
                break;
            }
    const Vec3 impulse = releaseImpulse(g.charge, g.scalar, true, kParameters);
    const float ratio = chargeRatio(g.charge, g.scalar, true, kParameters);
    if (!g.enemy && ratio > 0) {
        g.released = g.frozen;
        g.trailOffset = sub(g.lastPosition, position(g.frozen.address));
        g.trailRadius = radius(g.frozenBounds);
        g.trail.begin(g.lastPosition);
    }
    audio::play(ratio > 0.75f   ? audio::Cue::EndStrong
                : ratio > 0.35f ? audio::Cue::EndMedium
                                : audio::Cue::End);
    if (valid(body)) {
        stopBody(body);
        if (!g.enemy)
            applyImpulse(body, impulse);
    }
    const float fraction = std::fmax(g.remaining * (1.0f / g.duration), 0.0f);
    g.cooldown = std::fmax(kParameters.reloadFrames * (1.0f - fraction), 1.0f);
    Logging.Log("[stasis] RELEASE reason=%s hits=%u impulse=%f,%f,%f ratio=%f cooldown=%f body=%p",
                reason, g.strikes, double(impulse.x), double(impulse.y), double(impulse.z),
                double(ratio), double(g.cooldown), reinterpret_cast<void *>(body));
    g.frozen = g.selected = {};
    g.charge = {};
    g.remaining = 0;
    g.held = {};
}
// The game's hit tracker decides acceptance; this only drops repeats of one swing.
bool duplicate(const Hit &hit) {
    bool nonzero = false;
    for (unsigned id : hit.attack)
        nonzero |= id != 0;
    if (!nonzero)
        return false;
    for (unsigned i = 0; i < g.keyCount; ++i)
        if (g.keys[i].attacker == hit.attacker &&
            std::memcmp(g.keys[i].attack, hit.attack, sizeof(hit.attack)) == 0)
            return true;
    auto &key = g.keys[g.keyCursor];
    std::memcpy(key.attack, hit.attack, sizeof(hit.attack));
    key.attacker = hit.attacker;
    g.keyCursor = (g.keyCursor + 1) % 32;
    if (g.keyCount < 32)
        ++g.keyCount;
    return false;
}
void consumeHits() {
    HitQueue queue;
    signals.hits.tryUpdate([&](HitQueue &shared) {
        queue.count = shared.count;
        std::memcpy(queue.hits, shared.hits, shared.count * sizeof(Hit));
        shared.count = 0;
    });
    for (unsigned i = 0; i < queue.count; ++i) {
        const auto &hit = queue.hits[i];
        if (!g.frozen.address || hit.victim != g.frozen.address || hit.generation != g.generation)
            continue;
        if (duplicate(hit))
            continue;
        if (!finite(hit.normal) || !finite(hit.point)) {
            Logging.Log("[stasis] HIT_REFUSED nonfinite vector or point");
            continue;
        }
        ++g.strikes;
        g.hitFlash = 1.0f;
        audio::play(audio::Cue::Hit);
        if (!g.enemy) {
            const Strike strike{hit.direction, hit.point, hit.weapon, hit.playerStrike, true, true};
            (void)acceptStrike(g.charge, strike, g.scalar, true, kParameters);
        }
        Logging.Log("[stasis] HIT n=%u weapon=%d ids=%08x,%08x,%08x,%08x normal=%f,%f,%f charge=%f",
                    g.strikes, hit.weapon, hit.attack[0], hit.attack[1], hit.attack[2],
                    hit.attack[3], double(hit.normal.x), double(hit.normal.y), double(hit.normal.z),
                    double(length(g.charge.vector)));
    }
    if (const auto drops = signals.hitDrops.exchange(0))
        Logging.Log("[stasis] HIT_QUEUE_DROPPED count=%u", drops);
}
// Advances flashes, the release trail, hits and the hold timer by delta gameplay frames.
void advance(bool running, float delta) {
    g.trail.advance(delta);
    if (g.released.address && delta > 0) {
        if (g.trail.emitting() && seen(g.released))
            g.trail.record(add(position(g.released.address), g.trailOffset));
        else
            g.released = {};
    }
    g.hitFlash = std::fmax(g.hitFlash - delta / 8.0f, 0.0f);
    g.releaseFlash = std::fmax(g.releaseFlash - delta / 15.0f, 0.0f);
    if (g.frozen.address && !seen(g.frozen))
        resetSession("actor left roster");
    consumeHits();
    if (g.frozen.address) {
        g.remaining -= delta;
        const auto phase = static_cast<unsigned>(std::ceil(std::fmax(g.remaining, 0.0f) / 30.0f));
        if (running && phase > 0 && phase <= 3 && phase != g.countdownPhase) {
            g.countdownPhase = phase;
            audio::play(audio::Cue::Countdown);
        }
        if (g.remaining <= 0.0f && running)
            release("timer expired");
    } else if (g.cooldown > 0.0f)
        g.cooldown = std::fmax(g.cooldown - delta, 0.0f);
}
Presentation drawnState(float elapsedFrames) {
    Presentation visual;
    visual.previewOrigin = position(g.player.address);
    visual.aiming = g.aiming;
    visual.selected = g.aiming && g.selected.address != 0;
    visual.frozen = g.frozen.address != 0;
    visual.enemy = g.enemy;
    visual.remainingFrames = g.remaining;
    visual.durationFrames = g.duration;
    visual.cooldownFrames = g.cooldown;
    visual.elapsedFrames = elapsedFrames;
    visual.hitFlash = g.hitFlash;
    visual.releaseFlash = g.releaseFlash;
    visual.activationAge = visual.frozen ? (g.duration - g.remaining) / 30.0f : -1.0f;
    visual.trail = g.trail;
    visual.trailRadius = g.trailRadius;
    visual.surfaceActor = g.frozen.address ? g.frozen : g.selected;
    visual.scene = g.scene.token;
    if (const auto target = visual.surfaceActor; target.address && seen(target)) {
        if (visual.frozen)
            visual.bounds = g.frozenBounds;
        else
            worldBounds(target.address, visual.bounds);
        visual.position = centre(visual.bounds);
        visual.arrowOrigin = position(target.address);
    } else if (g.releaseFlash > 0.0f) {
        visual.position = g.lastPosition;
        visual.bounds = g.frozenBounds;
    }
    visual.direction = g.charge.vector;
    visual.ratio = chargeRatio(g.charge, g.scalar, true, kParameters);
    return visual;
}
} // namespace

void tick(void *device) {
    const auto frame = g_npad.read(device);
    const auto buttons = frame.snapshot().buttons;
    const bool freshSample = frame.snapshot().freshSampleCount != 0;
    const auto fresh = freshSample ? buttons & ~g.previousButtons : 0;
    if (freshSample) {
        ++g.tick;
        g.previousButtons = buttons;
    }
    const Clock clock{double(signals.clockTicks.load(std::memory_order_acquire)) / 4294967296.0,
                      signals.clockRunning.load(std::memory_order_acquire)};
    const auto unavailable = [&](const char *reason) {
        if (g.ready)
            resetSession(reason);
        g.ready = false;
        g.previousClock = clock.elapsedFrames;
    };
    const auto scene = g_scenes.observe(eng::resolveScene(moduleBase));
    if (!scene.context.isReady())
        return unavailable("world unavailable");
    if (g.scene.token != scene.context.token)
        resetSession("scene changed");
    g.scene = scene.context;
    const auto actors = eng::resolvePlayerActors(g.scene);
    if (!actors)
        return unavailable("player unavailable");
    g.player = actors.value.player;
    signals.recentPlayer.store(g.player.address, std::memory_order_release);
    const auto selections = signals.nativeSelections.load(std::memory_order_acquire);
    const bool otherChosen = g_otherAbilities.chosenElsewhere(eng::Ability::Stasis);
    if (selections != g_seenSelections || otherChosen) {
        g_seenSelections = selections;
        g_recent.reset();
        setAiming(false);
        signals.recentOwned.store(false, std::memory_order_release);
        Logging.Log("[stasis] RECENT_RELEASED mod=%u", unsigned(otherChosen));
    }
    g.ready = true;
    g.rosterCount = snapshotRoster(g.scene.token, g.roster, kRosterCapacity);
    const float delta = static_cast<float>(clock.elapsedFrames - g.previousClock);
    g.previousClock = clock.elapsedFrames;
    if (clock.running || delta > 0.0f)
        advance(clock.running, delta);

    constexpr auto A = input::A, B = input::B;
    const auto wheel = input::unpack(signals.wheel.load(std::memory_order_acquire));
    const auto decision = g_activation.step(wheel, buttons, fresh, freshSample, clock.running,
                                            !g.aiming && !g.frozen.address && g.cooldown <= 0.0f,
                                            g.frozen.address != 0, g.generation);
    signals.wheelRequest.store(g_activation.pendingEpoch, std::memory_order_release);
    if (decision.event != input::Event::None)
        Logging.Log("[stasis] WHEEL_ACTION event=%u epoch=%u open=%u exited=%u cancelled=%u "
                    "closed=%u running=%u cooldown=%f release=%u generation=%u",
                    unsigned(decision.event), wheel.epoch, unsigned(wheel.open),
                    unsigned(wheel.exited), unsigned(wheel.cancelled), unsigned(wheel.closed),
                    unsigned(clock.running), double(g.cooldown),
                    unsigned(decision.releaseFreeze || g_activation.pendingRelease), g.generation);
    if (decision.beginAim) {
        totk::engine::chooseAbility(totk::engine::Ability::Stasis);
        g_recent.remember();
        setAiming(true);
    }
    const bool quick = g_recent.step(wheel, buttons, fresh, freshSample, clock.running,
        !g.frozen.address && !eng::lHeldElsewhere(eng::Ability::Stasis),
        g_activation.pendingEpoch || g_activation.suppressL || decision.beginAim);
    if (quick) {
        setAiming(!g.aiming);
        Logging.Log("[stasis] RECENT_TAP aiming=%u", unsigned(g.aiming));
    }
    signals.recentOwned.store(g_recent.remembered, std::memory_order_release);
    std::uint64_t owned = decision.mask;
    if (decision.releaseFreeze)
        release("manual Camera wheel release");
    if (g.aiming) {
        owned |= A | B;
        selectTarget();
        if (clock.running && (fresh & B))
            setAiming(false);
        else if (clock.running && !decision.beginAim && (fresh & A))
            activate();
    }
    frame.maskOwnedButtons(owned);
    frame.addOwnedButtons(decision.add);
    signals.cameraRequest.tryWrite({g.player.address, g.scene.token.value,
                                    signals.cameraSession.load(std::memory_order_acquire),
                                    g.aiming});
    signals.presentation.tryWrite(clock.running ? drawnState(float(clock.elapsedFrames))
                                                : Presentation{});
}
} // namespace stasis::runtime
