// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "Runtime.hpp"
#include "Revival.hpp"
#include "Awareness.hpp"
#include "CircleMove.hpp"
#include "Companion.hpp"
#include "FollowFrame.hpp"
#include "Lifecycle.hpp"
#include "MemoryLog.hpp"
#include "NativeAction.hpp"
#include "NativeAnimation.hpp"
#include "NativeBrain.hpp"
#include "NativeGroundMotion.hpp"
#include "NativeInput.hpp"
#include "NativeLife.hpp"
#include "NativeMotion.hpp"
#include "NativeNavigation.hpp"
#include "NativePrompt.hpp"
#include "NativeSensor.hpp"
#include "NativeSignals.hpp"
#include "NativeStatus.hpp"
#include "NativeMove.hpp"
#include "Observation.hpp"
#include "RouteMove.hpp"
#include "SoundCues.hpp"
#include "TurnFacing.hpp"
#include "WorldMath.hpp"
#include "champions/Audio.hpp"
#include "champions/Effects.hpp"
#include "champions/FuryDamage.hpp"
#include "champions/Runtime.hpp"
#include "totk/engine/CodeCheck.hpp"
#include "totk/engine/Npad.hpp"
#include "totk/port/CodeCheck.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cmath>
#include <memory>
#include <numbers>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
using totk::core::WorldPosition;
constexpr std::uint64_t kClockRate = 30;  // clock ticks per second (the 30 Hz world step).
constexpr std::uint64_t kDayTicks = 86400;
constexpr float kWarpArrival = 20, kPi = std::numbers::pi_v<float>;
// The original's prompt reaches ten units across a sixty degree fan in front of Link.
constexpr float kPromptRadius = 10, kPromptAngle = 1.047f, kStandingStep = 0.01f;
constexpr std::uint64_t kButtonA = 1u << 0, kButtonZl = 1u << 8, kDpadDown = 1u << 15;
// The clock moves on alternate input frames, so five quiet frames in a row means a menu.
constexpr unsigned kMenuFrames = 5, kBrainSilence = 30;
// The original leads at a trot (gear 2); that could not stay ahead of a sprinting Link
// (wolf-compact-08), so on foot he leads at a canter.
constexpr int kLeadGear = 3, kFoodGear = 3, kFullGear = 4;
// The original's circling move child: AnimalMove at gear 3 with FinRadius 1.
constexpr int kCircleGear = 3;
constexpr float kCircleArrival = 1;
// Its aim takes the ground found from one unit above to ten below (our reading of the probe's limits).
constexpr float kCircleAbove = 1, kCircleBelow = 10;
// The original leads to a point this far ahead of the player, found again once the player has moved.
constexpr float kLeadAhead = 17.5f, kLeadSearch = 25, kLeadRefresh = 5;
constexpr int kLeadTries = 8;
// The original checks for ground within two units of the player. It checks every tick; this does not need to.
constexpr float kPathRadius = 2;
constexpr unsigned kPathCheckTicks = 8;
// Follow pacing in per-tick speed (m/s / 30): near, the wolf matches Link; past kCatchUpRange he runs ever faster.
constexpr float kIdleSpeed = 0.03f, kSprintSpeed = 0.21f, kPlayRange = 15;
constexpr float kBoostStart = 5, kBoostSlope = 0.06f, kBoostLimit = 1.4f;
constexpr float kCatchUpRange = 10, kCatchUpSlope = 0.15f, kCatchUpLimit = 3.0f;
// Nearer than this a player moving away does not count as leaving yet (HanSoloBlaster, wolf-compact-14: "a very
// small threshold - like 5 m").
constexpr float kFleeRange = 5;
constexpr float kHeadHeight = 1;
constexpr std::uint64_t kMemorySettle = 300, kMemoryPeriod = 3600;
// Ticks a dismissed wolf gets for its fade-out before the actor goes regardless.
constexpr std::uint64_t kDismissLimit = 240;
// A wolf lost to a loading screen comes back this long after the new place is running, and a wolf
// that has stood this long is settled enough that losing him again earns another return.
constexpr std::uint64_t kResummonDelay = 120, kSettledTicks = 600;
// game::scene::IsSmallDungeon(): true inside a shrine, where the original's wolf never goes.
constexpr totk::port::Code kIsSmallDungeon = 0x69A030;

// Set once at install, before either thread reads them.
std::uintptr_t g_base{};
bool g_routes{};

// Shared between the wheel, the AI worker and the controller thread.
enum class Warp : std::uint8_t { Idle, Requested, Moved, Failed };
std::atomic<std::uintptr_t> g_requestedScene{};
std::atomic<Warp> g_warp{Warp::Idle};
std::atomic<bool> g_died{}, g_replacing{};
std::atomic<bool> g_stayOrder{}, g_comeOrder{}, g_whistleOrder{}, g_zlHeld{};
// A dismiss goes to the brain for its fade-out and comes back once the clip has played.
std::atomic<bool> g_dismissOrder{}, g_left{};
// Either thread may need the prompt gone, and whichever finds it busy retries on its next tick.
std::atomic<PromptText> g_prompt{PromptText::None};
std::atomic<bool> g_promptAllowed{}, g_promptBusy{};
std::atomic<std::uint32_t> g_brainBeat{};

// This state is touched only by the controller thread.
Lifecycle g_lifecycle;
SceneTracker g_scene;
ActorStatus g_status = ActorStatus::Empty;
std::uint64_t g_previousClock{}, g_scanTicks{}, g_memoryDue{}, g_dismissDue{}, g_resummonDue{}, g_readyAt{};
bool g_recreate{}, g_memoryLeaving{}, g_aHeld{}, g_downHeld{};
// The wolf is out by the player's choice until a dismiss or a death; losing him otherwise brings him back.
bool g_summoned{}, g_cameBack{};
unsigned g_noGround{};
// Set when the wolf falls; the wheel reads them for its caption on its own thread.
std::atomic<std::uint64_t> g_diedAt{};
std::atomic<bool> g_everDied{};
unsigned g_arrivalChecks{}, g_quietFrames{}, g_brainQuiet{};
std::uint32_t g_lastBeat{};
totk::core::WorldTransform g_destination;
NpadReader g_input;

// This state is touched only by the serialized native AI callback.
struct Following {
    RouteMove route;
    FollowFrame frame;
    FollowSide side = FollowSide::Positive;
    std::uint32_t generation = UINT32_MAX;
    WorldPosition previous{};
    unsigned promptRoute{};
    CompanionStatus status{};
    WorldPosition leadGoal{}, leadPoint{}, leadAnchor{};
    bool event{}, groundSeen{}, noPath{}, routeFailed{}, leadPointSet{}, fleeing{};
    bool holding{}, holdSent{};  // Standing on the game's Wait, and whether that Wait has been taken.
    unsigned pathTicks{};
    float boost = 1;
    std::uint32_t paceKey = UINT32_MAX;
    HeadLook look;
    FleeLatch flee;
    CircleMove circle;
    CircleBand circleBand = CircleBand::None;
} g_follow;
Companion g_companion;
AnimationOverride g_animation;
const char* g_command{};
unsigned g_gone{};
bool g_overridden{};
SoundTimeline g_cues;
unsigned g_soundLogs{};
// The life the wolf last had, so a summon later the same day can pick it back up.
int g_leftLife = -1;
std::uint64_t g_leftAt{};

// Flat angle between the facing direction and the point; zero when either has no length.
float angleTo(const WorldPosition& from, const WorldPosition& forward, const WorldPosition& point) {
    const float x = point.x - from.x, z = point.z - from.z;
    const float distance = std::sqrt(x * x + z * z);
    const float length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    const float dot = distance > 0 && length > 0 ? (x * forward.x + z * forward.z) / (distance * length) : 1.0f;
    return std::acos(std::clamp(dot, -1.0f, 1.0f));
}

CompanionTarget describeTarget(const AwareActor& actor, const WorldPosition& wolf, const WorldPosition& forward) {
    if (!actor.present) return {};
    return {true, actor.flag, flatDistance(actor.position, wolf), angleTo(wolf, forward, actor.position),
            std::fabs(actor.position.y - wolf.y)};
}

std::optional<WorldPosition> surfacePoint(const WorldPosition& from, const WorldPosition& to) {
    const auto surface = sampleNavigationSurface(from, to);
    return surface ? std::optional{surface->position} : std::nullopt;
}

std::optional<WorldPosition> groundAround(const WorldPosition& point, float above, float below) {
    auto top = point, bottom = point;
    top.y += above;
    bottom.y -= below;
    return surfacePoint(top, bottom);
}

void setPrompt(PromptText wanted) {
    if (g_promptBusy.exchange(true, std::memory_order_acquire)) return;
    if (wanted != g_prompt.load(std::memory_order_relaxed)) {
        if (wanted == PromptText::None) {
            hidePrompt();
            g_prompt.store(wanted, std::memory_order_release);
        } else if (showPrompt(wanted)) {
            g_prompt.store(wanted, std::memory_order_release);
        } else {
            Logging.Log("[wolf-link] PROMPT_REFUSED text=%u", unsigned(wanted));
        }
    }
    g_promptBusy.store(false, std::memory_order_release);
}

// Native lock-on is the original's route. Holding the lock button at a wolf in the original's
// reach stands in if a code-spawned actor never becomes lockable.
void updatePrompt(const ActorReference& player, std::uint32_t wolf, const ActorKinematics& leader, const WorldPosition& position) {
    unsigned route = 0;
    if (g_promptAllowed.load(std::memory_order_acquire) && g_companion.listening()) {
        const auto lock = readLockOn(player, wolf);
        const auto& origin = leader.transform.position;
        const auto ahead = followWorldPoint(leader.transform, {0, 1});
        const WorldPosition forward{ahead.x - origin.x, ahead.y - origin.y, ahead.z - origin.z};
        if (lock == LockOn::Wolf) route = 1;
        else if (lock == LockOn::Nothing && g_zlHeld.load(std::memory_order_relaxed) &&
                 flatDistance(position, origin) <= kPromptRadius &&
                 angleTo(origin, forward, position) <= kPromptAngle) route = 2;
    }
    if (route != g_follow.promptRoute) {
        Logging.Log("[wolf-link] PROMPT route=%u ordered=%u", route, unsigned(g_companion.ordered()));
        g_follow.promptRoute = route;
    }
    setPrompt(!route ? PromptText::None : g_companion.ordered() ? PromptText::Come : PromptText::Stay);
}

WorldPosition followTarget(const ActorReference& actor, const ActorReference& player, const ActorKinematics& leader, const ActorKinematics& wolf) {
    auto& state = g_follow;
    const auto& frame = state.frame.transform;
    const auto& r = frame.rotation.values;
    const auto p = wolf.transform.position;
    const float localX = (p.x - frame.position.x) * r[0] +
                         (p.y - frame.position.y) * r[3] + (p.z - frame.position.z) * r[6];
    const auto width = readModelWidth(player);
    if (!width) return p;
    state.side = followSide(state.side, localX, *width);
    const auto offset = followOffset({}, originalFollowSpeed(leader.velocity), *width, localX, state.side);
    // Native Animal reachability stands in for the original terrain mapping.
    return selectFollowTarget(frame, offset, [&](WorldPosition candidate) {
        return probeFollowTarget(candidate, surfacePoint, [&](WorldPosition point) { return reachableFollowPoint(actor, point); });
    });
}

// The spec reads "player forward"; along the line to the sensor's target is assumed, since only
// that leads anywhere. Each failed try turns the line 45 degrees.
WorldPosition leadRoutePoint(Following& state, const WorldPosition& player) {
    if (state.leadPointSet && flatDistance(state.leadAnchor, player) <= kLeadRefresh) return state.leadPoint;
    const float x = state.leadGoal.x - player.x, z = state.leadGoal.z - player.z;
    const float length = std::sqrt(x * x + z * z);
    state.leadAnchor = player;
    state.leadPointSet = true;
    state.leadPoint = state.leadGoal;
    if (length <= kLeadAhead) return state.leadPoint;
    for (int i = 0; i < kLeadTries; ++i) {
        const float turn = float(i) * kPi / 4, c = std::cos(turn), s = std::sin(turn);
        const WorldPosition point{player.x + (x * c - z * s) / length * kLeadAhead, player.y,
                                  player.z + (x * s + z * c) / length * kLeadAhead};
        if (const auto surface = nearestNavigationSurface(point, kLeadSearch)) {
            state.leadPoint = surface->position;
            break;
        }
    }
    return state.leadPoint;
}

bool commandDone(const ActorReference& actor) {
    if (!g_command) return true;
    const auto animation = readAnimation(actor, g_command);
    if (!animation) return true;
    if (animation->current) {
        g_gone = 0;
        return animation->finished;
    }
    return ++g_gone > 3;
}

void playCommand(const ActorReference& actor, const CompanionOutput& output, float delta,
                 const WorldPosition& position, const WorldPosition& forward, const WorldPosition* aim) {
    g_follow.route.stop(actor);
    g_follow.holding = false;
    g_overridden = g_animation.acquire(actor);
    g_gone = 0;
    g_command = output.play;
    if (aim) {
        const auto facing = turnFacing(position, forward, *aim, kPi);
        static_cast<void>(setAnimationFloat(actor, "StickX", 0));
        static_cast<void>(setAnimationFloat(actor, "DirectionAngle", facing.animationAngle));
        // The attack graphs pick their clip by this: the short lunge at 3 units, the long one at 7.
        static_cast<void>(setAnimationFloat(actor, "Distance", flatDistance(position, *aim)));
    }
    if (!playAnimation(actor, output.play, output.rate, delta, false)) g_command = nullptr;
    g_cues.start(g_command, output.rate);
}

// The attack sounds come from the wolf's own sound user (tools/build_wolf_sound.py), so they play at him.
void playCues(float delta) {
    g_cues.advance(delta, [](const SoundCue& cue) {
        const bool played = champions::audio::playOn("BotwWolfLink", cue.call);
        if (g_soundLogs < 200) {
            ++g_soundLogs;
            Logging.Log("[wolf-link] SOUND call=%s command=%s played=%u", cue.call, cue.command, unsigned(played));
        }
    });
}

// The original's AnimalTurn re-aims the turn each frame until the clip reaches its no-change part; with
// RotateAngleMax pi and a negative FinishAngleRange the clip itself decides when the turn is over.
void steerTurn(const ActorReference& actor, const WorldPosition& position, const WorldPosition& forward,
               const WorldPosition* aim) {
    if (!aim || !g_command) return;
    const auto animation = readAnimation(actor, g_command);
    if (!animation || !animation->current || animation->noChange) return;
    static_cast<void>(setAnimationFloat(actor, "DirectionAngle", turnFacing(position, forward, *aim, kPi).animationAngle));
}

// A wolf that moves more than this in one tick was placed, not carried, by whatever ran last tick;
// the first such moves are logged with the state they interrupted (wolf-compact-08 resets, 2026-11-02).
constexpr float kJumpDistance = 2;
constexpr unsigned kJumpLogs = 40;
struct { std::uint32_t generation{}; WorldPosition position{}; unsigned logged{}; } g_jump;

void logJump(std::uint32_t generation, const WorldPosition& position, CompanionMode mode, float delta) {
    if (g_jump.generation == generation && g_jump.logged < kJumpLogs) {
        const float distance = flatDistance(position, g_jump.position);
        if (distance > kJumpDistance) {
            ++g_jump.logged;
            Logging.Log("[wolf-link] JUMP distance=%.1f rise=%.1f mode=%u command=%s override=%u delta=%.2f",
                double(distance), double(position.y - g_jump.position.y), unsigned(mode), g_command ? g_command : "-",
                unsigned(g_overridden), double(delta));
        }
    }
    g_jump.generation = generation;
    g_jump.position = position;
}

// The answer only counts once this summon has found ground at all, so a navigation query that never
// works reads as "path fine" and cannot freeze the wolf.
bool noGroundNearPlayer(Following& state, const WorldPosition& player) {
    const auto mode = g_companion.mode();
    if ((mode != CompanionMode::Follow && mode != CompanionMode::NoPath) || playerRiding()) return false;
    if (++state.pathTicks % kPathCheckTicks != 0) return state.noPath;
    const bool found = nearestNavigationSurface(player, kPathRadius).has_value();
    state.groundSeen |= found;
    const bool lost = state.groundSeen && !found;
    if (lost != state.noPath) Logging.Log("[wolf-link] PATH lost=%u", unsigned(lost));
    state.noPath = lost;
    return lost;
}

void applyLook(const ActorReference& actor, CompanionAim aim, const Awareness& seen, const WorldPosition& leader) {
    auto point = leader;
    switch (aim) {
    case CompanionAim::None:
        g_follow.look.rest(actor);
        return;
    case CompanionAim::Target: point = seen.target.position; break;
    case CompanionAim::Food: point = seen.food.position; break;
    case CompanionAim::Lead: point = g_follow.leadGoal; break;
    case CompanionAim::Leader: break;
    }
    if (aim != CompanionAim::Food) point.y += kHeadHeight;
    g_follow.look.aim(actor, point);
}

void applyOutput(const ActorReference& actor, const ActorReference& player, const CompanionOutput& output,
                 const Awareness& seen, const WorldPosition& position) {
    if (output.setLife >= 0) writeLife(actor, output.setLife);
    // Enemies only listen for Link's side (ReactionNoiseAffiliations), so the howl is Link's noise at the wolf's spot.
    if (output.alert) emitNoise(player, position);
    if (output.scatter) {
        for (unsigned i = 0; i < seen.nearbyCount; ++i) sendFlee(actor, seen.nearby[i], position);
        Logging.Log("[wolf-link] SCATTER animals=%u", seen.nearbyCount);
    }
    if (output.damage && seen.target.present) {
        const auto expires = champions::simulationTick() + (std::uint64_t{60} << 32);
        const bool queued = champions::fury_damage::queue({seen.target.address, seen.target.id},
            {.damage = std::uint16_t(output.damage), .physical = true}, expires);
        Logging.Log("[wolf-link] BITE id=%u damage=%d queued=%u", seen.target.id, output.damage, unsigned(queued));
    }
    if (output.avoidTarget && seen.target.present) avoidTarget(seen.target.id);
    if (output.rejectTarget > 0 && seen.target.present) restrictTarget(seen.target.id, output.rejectTarget);
    if (output.rejectFood && seen.food.present) restrictFood(seen.food.id);
    if (output.eat && seen.food.present) {
        consumeFood(seen.food.id);
        Logging.Log("[wolf-link] ATE id=%u recover=%d dropped=%u life=%d", seen.food.id, seen.food.recover,
            unsigned(seen.food.flag), g_companion.life());
    }
    if (output.appear || output.vanish) {
        // The sages' own materialize flash, pinned at the wolf's chest as he fades in or out.
        totk::core::WorldTransform flash{{{1, 0, 0, 0, 1, 0, 0, 0, 1}}, position};
        flash.position.y += 0.5f;
        const auto effect = output.appear ? champions::Effect::WolfArrive : champions::Effect::WolfLeave;
        if (!champions::effects::pulseAt(effect, 1.2f, flash))
            Logging.Log("[wolf-link] FLASH_REFUSED appear=%u", unsigned(output.appear));
    }
    if (output.warp) g_warp.store(Warp::Requested, std::memory_order_release);
    if (output.retire) g_died.store(true, std::memory_order_release);
    if (output.leave) g_left.store(true, std::memory_order_release);
}

void releaseCommand(const ActorReference& actor) {
    g_animation.release(actor);
    g_overridden = false;
    g_command = nullptr;
    g_cues.stop();
    g_follow.holding = false;
}

// Stay and the other holds stand on the game's own Wait with no leader and no steering. Running the
// close follow at his own spot with Link as the native leader walked him to Link at a crawl
// (wolf-compact-08). The Wait is asked for again while the action slot is still busy.
void holdStill(const ActorReference& actor) {
    auto& state = g_follow;
    if (!state.holding) {
        state.route.stop(actor);
        state.holding = true;
        state.holdSent = false;
    }
    if (state.holdSent) return;
    const auto applied = applyPathAction(actor, {0, 40, 0}, {2, 2, false, false}, true);
    if (applied == PathActionResult::Busy) return;
    state.holdSent = true;
    Logging.Log("[wolf-link] HOLD applied=%u mode=%u", unsigned(applied == PathActionResult::Applied),
        unsigned(g_companion.mode()));
}

// A new actor is a summon or a warp replacement; only a summon starts the companion afresh.
void beginActor(const ActorReference& actor) {
    auto& state = g_follow;
    state.route.stop(actor);
    setGroundSpeedScale(1);
    releaseCommand(actor);
    std::destroy_at(&state);
    std::construct_at(&state);
    state.generation = actor.generation();
    if (!g_replacing.exchange(false, std::memory_order_acq_rel)) {
        const bool sameDay = g_leftLife >= 0 && (champions::simulationTick() >> 32) - g_leftAt < kDayTicks;
        g_companion = Companion(actor.generation());
        g_companion.arrive(false, sameDay ? g_leftLife : -1);
        Logging.Log("[wolf-link] SUMMON carried-life=%d", sameDay ? g_leftLife : -1);
    }
    Logging.Log("[wolf-link] AI_BEGIN id=%u", state.generation);
    logObservation(actor, g_base);
}

void readOrders(CompanionInput& input) {
    input.stay = g_stayOrder.exchange(false, std::memory_order_acq_rel);
    input.come = g_comeOrder.exchange(false, std::memory_order_acq_rel);
    input.whistle = g_whistleOrder.exchange(false, std::memory_order_acq_rel);
    input.dismiss = g_dismissOrder.exchange(false, std::memory_order_acq_rel);
    if (input.stay || input.come || input.whistle || input.dismiss) {
        Logging.Log("[wolf-link] ORDER stay=%u come=%u whistle=%u dismiss=%u mode=%u listening=%u", unsigned(input.stay),
            unsigned(input.come), unsigned(input.whistle), unsigned(input.dismiss), unsigned(g_companion.mode()),
            unsigned(g_companion.listening()));
    }
}

void readWarpResult(CompanionInput& input) {
    const auto warp = g_warp.load(std::memory_order_acquire);
    input.warpFinished = warp == Warp::Moved;
    input.warpFailed = warp == Warp::Failed;
    if (input.warpFinished || input.warpFailed) g_warp.store(Warp::Idle, std::memory_order_release);
}

CompanionInput readInput(const ActorReference& actor, float delta, const WorldPosition& leader,
                         const WorldPosition& position, const WorldPosition& forward, const Awareness& seen) {
    auto& state = g_follow;
    CompanionInput input;
    input.delta = delta;
    input.playerDistance = flatDistance(position, leader);
    input.leaderAngle = angleTo(position, forward, leader);
    input.standing = flatDistance(position, state.previous) < kStandingStep;
    state.previous = position;
    readOrders(input);
    input.deepWater = seen.deepWater;
    input.animationDone = commandDone(actor);
    readWarpResult(input);
    if (const auto life = readLife(actor)) input.life = *life;
    const auto hits = takeLifeHits();
    input.hit = hits.damage;
    if (hits.damage || hits.playerDamage) {
        Logging.Log("[wolf-link] HIT damage=%d player-damage=%d life=%d mode=%u", hits.damage, hits.playerDamage,
            g_companion.life(), unsigned(g_companion.mode()));
    }
    if (const auto goal = readSensorTarget()) {
        state.leadGoal = *goal;
        input.lead = {true, flatDistance(*goal, position), flatDistance(*goal, leader)};
    }
    input.status = readStatus(actor);
    if (std::memcmp(&input.status, &state.status, sizeof(CompanionStatus))) {
        Logging.Log("[wolf-link] STATUS burning=%u frozen=%u shocked=%u gust=%u hit=%u mode=%u", unsigned(input.status.burning),
            unsigned(input.status.frozen), unsigned(input.status.shocked), unsigned(input.status.gust),
            unsigned(input.status.hitStrength), unsigned(g_companion.mode()));
        state.status = input.status;
    }
    input.target = describeTarget(seen.target, position, forward);
    if (seen.food.present) {
        input.food = {true, seen.food.flag, flatDistance(seen.food.position, position),
                      std::fabs(seen.food.position.y - position.y), seen.food.recover};
    }
    input.event = mainEventPlaying();
    if (input.event != state.event) {
        Logging.Log("[wolf-link] EVENT playing=%u mode=%u", unsigned(input.event), unsigned(g_companion.mode()));
        state.event = input.event;
    }
    input.noPath = noGroundNearPlayer(state, leader);
    input.routeFailed = state.routeFailed;
    state.routeFailed = false;
    return input;
}

void logModeChange(CompanionMode before, const CompanionInput& input, const Awareness& seen) {
    const auto mode = g_companion.mode();
    if (mode == before) return;
    Logging.Log("[wolf-link] MODE %u->%u life=%d charge=%.1f fed=%u target=%u food=%u",
        unsigned(before), unsigned(mode), g_companion.life(), double(g_companion.charge()),
        unsigned(g_companion.fed()), unsigned(seen.target.present), unsigned(seen.food.present));
    if (mode == CompanionMode::Lead) {
        const auto& goal = g_follow.leadGoal;
        Logging.Log("[wolf-link] LEAD goal=%.1f,%.1f,%.1f distance=%.1f riding=%u", double(goal.x),
            double(goal.y), double(goal.z), double(input.lead.distance), unsigned(playerRiding()));
    }
}

// Riding-along check for the wrong-way bites (wolf-compact-18): the enemy's bearing from the drawn body,
// which now steers, and from the movement controller, which steered before, at each pre-bite turn and bite.
void logFacing(CompanionMode before, const CompanionInput& input, const Awareness& seen, const WorldPosition& position,
               const WorldPosition& controller) {
    const auto mode = g_companion.mode();
    const auto watched = [](CompanionMode value) {
        return value == CompanionMode::AttackTurn || value == CompanionMode::Strike || value == CompanionMode::ChainStrike;
    };
    if (mode == before || !(watched(mode) || watched(before)) || !seen.target.present) return;
    constexpr float kDegrees = 180 / kPi;
    Logging.Log("[wolf-link] FACING mode=%u->%u body=%.0f controller=%.0f distance=%.1f", unsigned(before),
        unsigned(mode), double(input.target.angle * kDegrees),
        double(angleTo(position, controller, seen.target.position) * kDegrees), double(input.target.distance));
}

// Riding-along check for the catch-up stop (wolf-compact-19): what was moving the wolf when follow gave up
// on him, and how far he stood from his follow point.
void logFollowStall(CompanionMode before, const CompanionInput& input, const ActorReference& actor,
                    const ActorReference& player, const ActorKinematics& leader, const ActorKinematics& wolf) {
    const auto mode = g_companion.mode();
    if (before != CompanionMode::Follow || (mode != CompanionMode::FollowRetry && mode != CompanionMode::WarpOut)) return;
    const auto station = followTarget(actor, player, leader, wolf);
    Logging.Log("[wolf-link] FOLLOW_STALL distance=%.1f station=%.1f owner=%u gear=%d still=%u route-failed=%u",
        double(input.playerDistance), double(flatDistance(wolf.transform.position, station)), g_follow.route.owner(),
        g_follow.route.closeGear(), unsigned(input.leaderStill), unsigned(input.routeFailed));
}

// The catch-up boost rides only the follow; every other state moves at the animation's own pace.
void updatePace(const CompanionInput& input, const CompanionOutput& output, const ActorKinematics& leader) {
    float boost = 1;
    if (output.move == CompanionMove::Follow) {
        if (input.playerDistance > kCatchUpRange)
            boost = std::clamp(1.0f + (input.playerDistance - kCatchUpRange) * kCatchUpSlope, 1.0f, kCatchUpLimit);
        else if (originalFollowSpeed(leader.velocity) >= kIdleSpeed)
            boost = std::clamp(1.0f + (input.playerDistance - kBoostStart) * kBoostSlope, 1.0f, kBoostLimit);
    }
    g_follow.boost = boost;
    setGroundSpeedScale(boost);
}

void logPace(const CompanionInput& input, const RouteRequest& route) {
    const int gear = g_follow.route.closeGear();
    const float distance = input.playerDistance;
    const unsigned band = distance < 4 ? 0u : distance < 8 ? 1u : distance < 16 ? 2u : 3u;
    const std::uint32_t key = std::uint32_t(gear + 1) | std::uint32_t(route.gearFloor) << 8 |
        std::uint32_t(route.gearLimit) << 12 | band << 16 | std::uint32_t(g_follow.boost * 20) << 20;
    if (key == g_follow.paceKey) return;
    g_follow.paceKey = key;
    Logging.Log("[wolf-link] PACE distance=%.1f gear=%d floor=%d limit=%d boost=%.2f",
        double(distance), gear, route.gearFloor, route.gearLimit, double(g_follow.boost));
}

const WorldPosition* commandAim(CompanionAim aim, const Awareness& seen, const WorldPosition& leader) {
    if (aim == CompanionAim::Leader) return &leader;
    if (aim == CompanionAim::Target && seen.target.present) return &seen.target.position;
    if (aim == CompanionAim::Food && seen.food.present) return &seen.food.position;
    return nullptr;
}

// The original's circling child restarts whenever its band changes or the wolf comes back to it, as the
// original re-enters it after each attack. Nothing means every aim failed; the wolf stands that tick.
std::optional<WorldPosition> circleAim(const CompanionOutput& output, const Awareness& seen,
                                       const WorldPosition& position, const WorldPosition& facing, float delta) {
    if (output.move != CompanionMove::Circle || !seen.target.present) return std::nullopt;
    if (output.circle != g_follow.circleBand) {
        g_follow.circleBand = output.circle;
        g_follow.circle.begin(output.circle == CircleBand::Leave   ? kLeaveCircle
                              : output.circle == CircleBand::Outer ? kOuterCircle
                                                                   : kInnerCircle);
        Logging.Log("[wolf-link] CIRCLE band=%u distance=%.1f", unsigned(output.circle),
            double(flatDistance(position, seen.target.position)));
    }
    return g_follow.circle.update(position, facing, seen.target.position, delta,
        [](const WorldPosition& aim) { return groundAround(aim, kCircleAbove, kCircleBelow); });
}

RouteRequest chooseRoute(const ActorReference& actor, const ActorReference& player, const ActorKinematics& leader,
                         const ActorKinematics& wolf, const CompanionOutput& output, const CompanionInput& input,
                         const Awareness& seen, const std::optional<WorldPosition>& circle) {
    RouteRequest route{.keptDistance = output.keptDistance, .delta = input.delta};
    route.target = wolf.transform.position;
    if (output.move == CompanionMove::Follow) {
        route.target = followTarget(actor, player, leader, wolf);
        route.predict = route.routes = true;
        route.routeDistance = kFollowRouteDistance;
        route.frame = &wolf.transform;
        const float pace = originalFollowSpeed(leader.velocity);
        const float distance = input.playerDistance;
        // A planned catch-up route also runs at full gear; it was left to the corridor checks, which
        // chose slower gaits (HanSoloBlaster, wolf-compact-18: "He doesn't run at full gallop").
        if (distance > kCatchUpRange) route.gearFloor = route.routeGear = kFullGear;
        else if (pace >= kSprintSpeed) route.gearFloor = distance > 3.5f ? kFullGear : 3;
        else if (pace >= kIdleSpeed) route.gearFloor = distance > 6 ? 3 : distance > 3 ? 2 : 0;
        else if (distance <= kPlayRange) route.gearLimit = distance > 6 ? 2 : 1;
    } else if (output.move == CompanionMove::Chase && seen.target.present) {
        // Always a planned path at a full run, never a straight line at the enemy; a refused route is
        // the companion's to wait out, so the chase has no cliff child.
        const auto mode = g_companion.mode();
        const bool rush = mode == CompanionMode::Close || mode == CompanionMode::ChainClose;
        route.target = seen.target.position;
        route.routes = route.chase = true;
        route.routeGear = kFullGear;
        route.arrival = rush ? kRushTolerance : kBattleArrival;
        route.goal = rush ? PathGoal{kRushTolerance, false} : PathGoal{kBattleTolerance, true};
        route.retarget = rush ? kRushRetarget : RoutePlan::kNearRetarget;
    } else if (output.move == CompanionMove::Circle && circle) {
        route.target = *circle;
        route.gearLimit = route.gearFloor = kCircleGear;
        route.keptDistance = kCircleArrival;
    } else if (output.move == CompanionMove::ToFood && seen.food.present) {
        route.target = seen.food.position;
        route.routes = true;
        route.routeDistance = kFoodRouteDistance;
        route.routeGear = kFoodGear;
    } else if (output.move == CompanionMove::Lead) {
        // A full run beside a rider, as in the original.
        route.gearLimit = route.routeGear = playerRiding() ? kFullGear : kLeadGear;
        route.target = leadRoutePoint(g_follow, leader.transform.position);
        route.routes = route.always = true;
    }
    route.routes = route.routes && g_routes;
    if (!g_routes) route.frame = nullptr;
    route.leaderDistance = input.playerDistance;
    return route;
}

void updateBrain(const ActorReference& actor, BrainTick tick) {
    if (!std::isfinite(tick.delta) || tick.delta <= 0) return;
    const auto scene = resolveScene(g_base);
    if (!scene) return;
    const auto player = borrowPlayer(scene.value);
    watchLife(actor.address(), player.address());
    const auto leader = readKinematics(player);
    const auto wolf = readKinematics(actor);
    const auto motion = readMotion(actor);
    if (!leader || !wolf || !motion) return;
    const bool first = g_follow.generation != actor.generation();
    if (first) beginActor(actor);
    updateFollowFrame(g_follow.frame, leader->transform, first);
    const auto seen = surroundings();
    const auto position = wolf->transform.position;
    const auto& leaderPosition = leader->transform.position;
    g_brainBeat.fetch_add(1, std::memory_order_relaxed);
    const auto facing = bodyForward(wolf->transform);
    auto input = readInput(actor, tick.delta, leaderPosition, position, facing, seen);
    // The player is leaving when his velocity points away from the wolf past a small gap; sprinting
    // away pulls the wolf out of meals, hunts and fights (HanSoloBlaster's wolf-compact-14 directives).
    const float pace = originalFollowSpeed(leader->velocity);
    const bool receding = leader->velocity.x * (leaderPosition.x - position.x) +
                          leader->velocity.z * (leaderPosition.z - position.z) > 0;
    input.leaderLeaving = receding && pace >= kIdleSpeed && input.playerDistance > kFleeRange;
    input.leaderStill = pace < kIdleSpeed;
    input.leaderFleeing = g_follow.flee.update(input.leaderLeaving && pace >= kSprintSpeed, input.playerDistance,
                                                tick.delta);
    if (input.leaderFleeing != g_follow.fleeing) {
        g_follow.fleeing = input.leaderFleeing;
        Logging.Log("[wolf-link] FLEE on=%u distance=%.1f mode=%u", unsigned(input.leaderFleeing),
            double(input.playerDistance), unsigned(g_companion.mode()));
    }
    const auto mode = g_companion.mode();
    logJump(actor.generation(), position, mode, tick.delta);
    const auto output = g_companion.step(input);
    g_leftLife = g_companion.life();
    g_leftAt = champions::simulationTick() >> 32;
    logModeChange(mode, input, seen);
    logFacing(mode, input, seen, position, motion->direction);
    logFollowStall(mode, input, actor, player, *leader, *wolf);
    if (output.gaveUp) {
        Logging.Log("[wolf-link] GIVE_UP reason=%s distance=%.1f angle=%.2f height=%.1f mode=%u", output.gaveUp,
            double(input.target.distance), double(input.target.angle), double(input.target.height), unsigned(mode));
    }
    applyOutput(actor, player, output, seen, position);
    applyLook(actor, output.look, seen, leaderPosition);
    updatePace(input, output, *leader);
    updatePrompt(player, actor.generation(), *leader, position);
    if (output.move != CompanionMove::Circle) g_follow.circleBand = CircleBand::None;
    if (output.play) {
        playCommand(actor, output, tick.delta, position, facing, commandAim(output.aim, seen, leaderPosition));
        playCues(tick.delta);
        return;
    }
    if (output.move == CompanionMove::Script) {
        if (output.steer) steerTurn(actor, position, facing, commandAim(output.aim, seen, leaderPosition));
        playCues(tick.delta);
        return;
    }
    g_cues.stop();
    if (g_overridden) releaseCommand(actor);
    const auto circle = circleAim(output, seen, position, facing, tick.delta);
    if (output.move == CompanionMove::Hold || (output.move == CompanionMove::Circle && !circle)) {
        holdStill(actor);
        return;
    }
    g_follow.holding = false;
    const auto route = chooseRoute(actor, player, *leader, *wolf, output, input, seen, circle);
    g_follow.routeFailed = g_follow.route.update(actor, player, position, route) == FollowStatus::Failed;
    if (output.move == CompanionMove::Follow) logPace(input, route);
}

// Warp probes: five units ahead, 30 degrees either side, then beside, behind, and last the player's own spot.
std::optional<WorldPosition> warpPoint(const totk::core::WorldTransform& player) {
    constexpr float kOffsets[][2]{{0, 5.0f}, {3.0f, 5.0f}, {-3.0f, 5.0f},
                                  {2.5f, 0.5f}, {-2.5f, 0.5f}, {0, -3.0f}, {0, 0}};
    for (const auto& offset : kOffsets) {
        if (auto ground = groundAround(followWorldPoint(player, {offset[0], offset[1]}), 4, 8)) {
            ground->y += 0.1f;
            return ground;
        }
    }
    return std::nullopt;
}

void serviceWarp(const ActorReference& actor, const totk::core::WorldTransform& player) {
    if (g_arrivalChecks) {
        const auto wolf = readKinematics(actor);
        if (wolf && flatDistance(wolf->transform.position, g_destination.position) <= kWarpArrival) {
            g_arrivalChecks = 0;
            g_warp.store(Warp::Moved, std::memory_order_release);
        } else if (--g_arrivalChecks == 0) {
            // The actor kept its old place, so replace it at the destination.
            Logging.Log("[wolf-link] WARP_REPLACE");
            g_recreate = true;
            g_replacing.store(true, std::memory_order_release);
            g_lifecycle.retire();
        }
        return;
    }
    if (g_warp.load(std::memory_order_acquire) != Warp::Requested) return;
    const auto point = warpPoint(player);
    if (!point) {
        // The brain asks again every second while the player is over water; log the first few and then every thirtieth.
        if (++g_noGround <= 8 || g_noGround % 30 == 0) Logging.Log("[wolf-link] WARP_NO_GROUND count=%u", g_noGround);
        g_warp.store(Warp::Failed, std::memory_order_release);
        return;
    }
    g_noGround = 0;
    g_destination = player;
    g_destination.position = *point;
    if (moveActor(actor, g_destination)) g_arrivalChecks = 8;
    else g_warp.store(Warp::Failed, std::memory_order_release);
}

// A press counts only as a new press, so an A held through a prompt opening gives no order.
void serviceOrders(void* device, bool live) {
    const auto frame = g_input.read(device);
    const auto& input = frame.snapshot();
    if (!input.freshSampleCount) return;
    const bool a = input.held(kButtonA), down = input.held(kDpadDown);
    g_zlHeld.store(live && input.held(kButtonZl), std::memory_order_relaxed);
    if (live) {
        const auto prompt = g_prompt.load(std::memory_order_acquire);
        if (prompt != PromptText::None) {
            frame.maskOwnedButtons(kButtonA);
            if (a && !g_aHeld) (prompt == PromptText::Come ? g_comeOrder : g_stayOrder).store(true, std::memory_order_release);
        }
        if (down && !g_downHeld) g_whistleOrder.store(true, std::memory_order_release);
    }
    g_aHeld = a;
    g_downHeld = down;
}

// Returns whether game time moved since the last poll.
bool trackGameClock(std::uint64_t clock) {
    const bool running = clock != g_previousClock;
    g_previousClock = clock;
    g_quietFrames = running ? 0 : std::min(g_quietFrames + 1, kMenuFrames);
    const auto beat = g_brainBeat.load(std::memory_order_relaxed);
    g_brainQuiet = beat != g_lastBeat ? 0 : std::min(g_brainQuiet + unsigned(running), kBrainSilence);
    g_lastBeat = beat;
    return running;
}

void serviceSummonedWolf(const SceneContext& scene, const totk::core::WorldTransform& player, std::uint64_t ticks) {
    const auto wolf = g_lifecycle.borrow();
    if (const auto own = readKinematics(wolf)) {
        if (++g_scanTicks % 6 == 0) scanSurroundings(scene, wolf.generation(), own->transform.position, player.position);
        serviceWarp(wolf, player);
    }
    if (ticks >= g_memoryDue) {
        logActorHeap(wolf, "summoned");
        logResourceMemory("summoned");
        g_memoryDue = ticks + kMemoryPeriod;
    }
}

void finishDismiss(ActorStatus status, std::uint64_t ticks) {
    if (status == ActorStatus::Ready) logActorHeap(g_lifecycle.borrow(), "before-dismiss");
    logResourceMemory("before-dismiss");
    g_memoryDue = ticks + kMemorySettle;
    g_memoryLeaving = true;
    g_dismissDue = 0;
    g_summoned = false;
    watchLife(0, 0);
    g_lifecycle.retire();
    clearSurroundings();
    g_warp.store(Warp::Idle, std::memory_order_release);
    g_arrivalChecks = 0;
    g_recreate = false;
    Logging.Log("[wolf-link] DISMISS");
}

// A dismissed wolf fades out on his own clip before the actor goes (wolf-compact-08 had him vanish
// at once). A wolf whose brain is not ticking goes at once; otherwise the actor goes when the brain
// reports the clip played or the wait runs out.
void dismiss(ActorStatus status, std::uint64_t ticks) {
    if (g_dismissDue) return;
    if (status != ActorStatus::Ready || g_brainQuiet >= kBrainSilence) {
        finishDismiss(status, ticks);
        return;
    }
    g_left.store(false, std::memory_order_release);
    g_dismissOrder.store(true, std::memory_order_release);
    g_dismissDue = ticks + kDismissLimit;
    g_summoned = false;
    Logging.Log("[wolf-link] DISMISS_FADE");
}

void serviceDismiss(ActorStatus status, std::uint64_t ticks) {
    if (!g_dismissDue) return;
    if (status != ActorStatus::Ready) {
        g_dismissDue = 0;  // The actor already left another way.
        return;
    }
    const bool left = g_left.exchange(false, std::memory_order_acq_rel);
    if (!left && ticks < g_dismissDue) return;
    Logging.Log("[wolf-link] DISMISS_FADE played=%u", unsigned(left));
    finishDismiss(status, ticks);
}

// Ticks left in the rest a fallen wolf serves before he may be summoned again; zero once he may.
std::uint64_t restAfterFalling(std::uint64_t ticks) {
    const bool died = g_everDied.load(std::memory_order_acquire);
    return revivalRemaining(ticks, g_diedAt.load(std::memory_order_acquire), died);
}

void summon(const std::optional<ActorKinematics>& player, std::uint64_t ticks) {
    if (const auto remaining = restAfterFalling(ticks)) {
        Logging.Log("[wolf-link] SUMMON_REFUSED resting remaining=%llu", static_cast<unsigned long long>(remaining));
        return;
    }
    if (!player) {
        Logging.Log("[wolf-link] SUMMON_REFUSED player-unavailable");
        return;
    }
    clearSurroundings();
    auto transform = player->transform;
    const auto point = followWorldPoint(transform, {0, 3.5f});
    transform.position = groundAround(point, 2.0f, 5.0f).value_or(point);
    transform.position.y += 0.1f;
    logResourceMemory("before-summon");
    g_memoryDue = ticks + kMemorySettle;
    g_memoryLeaving = false;
    static_cast<void>(g_lifecycle.create(transform));
}

// A wolf that goes with no dismiss and no death was lost to a loading screen (fast travel,
// wolf-compact-08). The original's wolf comes back after one, so this one is summoned again once
// the new place is running; a wolf lost again before he has settled is not, so a place that
// cannot keep him cannot summon him over and over. A warp retires and recreates the actor
// without losing him, so it neither clears the summons nor restarts the settling.
void noteLoss(ActorStatus status, std::uint64_t ticks) {
    if (status == ActorStatus::Ready) {
        if (!g_summoned) g_readyAt = ticks;
        g_summoned = true;
        return;
    }
    if ((status != ActorStatus::Empty && status != ActorStatus::Failed) || !g_summoned || g_recreate) return;
    g_summoned = false;
    if (g_cameBack && ticks - g_readyAt < kSettledTicks) {
        Logging.Log("[wolf-link] LOST comeback=0 reason=lost-again");
        return;
    }
    g_resummonDue = ticks + kResummonDelay;
    Logging.Log("[wolf-link] LOST comeback=1");
}

void serviceComeback(const std::optional<ActorKinematics>& player, ActorStatus status, std::uint64_t ticks) {
    if (!g_resummonDue || ticks < g_resummonDue || status != ActorStatus::Empty) return;
    g_resummonDue = 0;
    const bool shrine = reinterpret_cast<bool (*)()>(g_base + kIsSmallDungeon)();
    Logging.Log("[wolf-link] COMEBACK shrine=%u", unsigned(shrine));
    if (shrine) return;
    g_cameBack = true;
    summon(player, ticks);
}

void serviceWorld(const SceneContext& scene, ActorStatus status, std::uint64_t ticks) {
    if (g_died.exchange(false, std::memory_order_acq_rel)) {
        g_diedAt.store(ticks, std::memory_order_release);
        g_everDied.store(true, std::memory_order_release);
        g_summoned = false;
        g_resummonDue = 0;
        watchLife(0, 0);
        g_lifecycle.retire();
        Logging.Log("[wolf-link] DIED");
    }
    if (g_recreate && status == ActorStatus::Empty) {
        g_recreate = false;
        g_warp.store(g_lifecycle.create(g_destination) ? Warp::Moved : Warp::Failed, std::memory_order_release);
    }
    serviceDismiss(status, ticks);
    const auto player = readKinematics(borrowPlayer(scene));
    serviceComeback(player, status, ticks);
    if (status == ActorStatus::Ready && player) serviceSummonedWolf(scene, player->transform, ticks);
    if (g_memoryLeaving && status == ActorStatus::Empty && ticks >= g_memoryDue) {
        g_memoryLeaving = false;
        logResourceMemory("after-dismiss");
    }
    const auto request = g_requestedScene.exchange(0, std::memory_order_acq_rel);
    if (!request || request != scene.token.value) return;
    if (status == ActorStatus::Creating || status == ActorStatus::Ready) {
        dismiss(status, ticks);
    } else if (status != ActorStatus::Retiring) {
        g_resummonDue = 0;
        g_cameBack = false;
        summon(player, ticks);
    }
}

#ifdef TOTK_SUITE_MEMBER
std::atomic<bool> g_switchedOn{true};
bool g_offApplied{};
#endif

void service(void* device) {
    const auto scene = g_scene.observe(resolveScene(g_base));
    const auto status = g_lifecycle.update(scene);
    const auto clock = champions::simulationTick();
    if (status != g_status) {
        Logging.Log("[wolf-link] LIFECYCLE state=%u", unsigned(status));
        noteLoss(status, clock >> 32);
        g_status = status;
    }
#ifdef TOTK_SUITE_MEMBER
    if (g_switchedOn.load(std::memory_order_acquire)) {
        g_offApplied = false;
    } else {
        // Sends the wolf away at once (no fade) and cancels any summon or comeback; the lifecycle update still runs.
        if (!g_offApplied) {
            if (status == ActorStatus::Creating || status == ActorStatus::Ready) finishDismiss(status, clock >> 32);
            g_offApplied = true;
            Logging.Log("[wolf-link] SWITCHED_OFF state=%u", unsigned(status));
        }
        g_summoned = g_cameBack = g_recreate = false;
        g_resummonDue = 0;
        g_requestedScene.store(0, std::memory_order_release);
        g_promptAllowed.store(false, std::memory_order_release);
        setPrompt(PromptText::None);
        return;
    }
#endif
    const bool running = trackGameClock(clock);
    const bool prompting = scene.context.isReady() && status == ActorStatus::Ready && g_brainQuiet < kBrainSilence;
    g_promptAllowed.store(prompting, std::memory_order_release);
    if (!prompting) setPrompt(PromptText::None);
    serviceOrders(device, prompting && g_quietFrames < kMenuFrames);
    if (!scene.context.isReady()) g_requestedScene.store(0, std::memory_order_release);
    else if (running) serviceWorld(scene.context, status, clock >> 32);
}

HOOK_DEFINE_TRAMPOLINE(Controller) {
    static void Callback(void* device) { Orig(device); service(device); }
};
}

#ifdef TOTK_SUITE_MEMBER
void switched(bool on) { g_switchedOn.store(on, std::memory_order_release); }
#endif

bool chooseFromWheel(std::uintptr_t) {
#ifdef TOTK_SUITE_MEMBER
    if (!g_switchedOn.load(std::memory_order_acquire)) return false;
#endif
    if (!canSummon()) return false;
    const auto scene = resolveScene(g_base);
    if (!scene) return false;
    g_requestedScene.store(scene.value.token.value, std::memory_order_release);
    return true;
}

bool canSummon() { return restAfterFalling(champions::simulationTick() >> 32) == 0; }

// Highlighting the Wolf wedge while he is resting shows how much longer, rounded up to whole minutes;
// null lets the wedge keep its own name once he may be summoned again.
const char16_t* wheelCaption() {
    const auto remaining = restAfterFalling(champions::simulationTick() >> 32);
    if (!remaining) return nullptr;
    const auto seconds = (remaining + kClockRate - 1) / kClockRate;
    static char16_t text[48];
    char16_t* out = text;
    for (const char16_t* p = u"Wolf Link is resting"; *p;) *out++ = *p++;
    if (seconds > 60) {
        for (const char16_t* p = u" (about "; *p;) *out++ = *p++;
        *out++ = char16_t(u'0' + (seconds + 59) / 60);  // one to seven minutes left
        for (const char16_t* p = u" min)"; *p;) *out++ = *p++;
    } else {
        for (const char16_t* p = u" (under a minute)"; *p;) *out++ = *p++;
    }
    *out = 0;
    return text;
}

bool install(std::uintptr_t base) {
    using namespace totk::port::literals;
    constexpr totk::port::Site sites[]{{0x2A267BC, 0xD102C3FF, 0xA9057BFD}, {kIsSmallDungeon, 0xA9BE7BFD, 0xF9000BF3}};
    if (!totk::port::codeMatches(base, sites, "wolf-link", "RUNTIME_REFUSED") ||
        !initializeInputApi(base) || !initializeNavigationApi(base) || !initializeMotionApi(base) ||
        !initializeAnimationApi(base) || !initializeActionApi(base) || !initializeAwarenessApi(base) ||
        !initializePromptApi(base) || !initializeStatusApi(base) || !installLifeApi(base) || !initializeSensorApi(base) ||
        !initializeSignalApi(base) || !installBrainApi(base, updateBrain) || !installObservation(base)) return false;
    g_base = base;
    initializeMemoryLog(base);
    g_routes = initializeMoveApi(base);
    if (!g_routes) Logging.Log("[wolf-link] ROUTES_UNAVAILABLE running-straight=1");
    Controller::InstallAtOffset(0x2A267BC_v121);
    Logging.Log("[wolf-link] COMPANION summon-dismiss=wheel-right state-bytes=%zu",
        sizeof(g_follow) + sizeof(g_companion) + sizeof(g_lifecycle) + sizeof(g_scene));
    return true;
}
}
