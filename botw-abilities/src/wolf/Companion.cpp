// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Companion.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace botw_abilities::wolf {
namespace {
constexpr float kWarpDistance = 40, kBattleRange = 45, kHealRange = 30, kHeightLimit = 7.5f;
// Beyond this the wolf drops a fight or a meal and rejoins the player instead of lingering (Clay,
// wolf-compact-11: he should leave an NPC or a fox once the player is far enough). Kept below the warp
// distance so he walks back before a warp is needed. The entry gate alone did not stop a fight already
// running (Clay, wolf-compact-12: he does his own thing while Link is far away), so an ongoing fight
// or meal also breaks off at kLeashBreak; the gap between the two keeps the edge from flapping.
constexpr float kLeashRange = 28, kLeashBreak = 32;
constexpr float kInitiateBattle = 16, kInitiateHunt = 45, kBeginAttack = 5, kKeepRange = 11;
// The original's battle band (AnimalRangeKeepMoveWithLOS): between attacks the wolf closes in from beyond
// CloseStartDist until inside CloseEndDist, then circles the enemy; inside LeaveStartDist it backs off
// to a wider circle until beyond LeaveEndDist. Changed in wolf-compact-20 from standing still inside
// CloseEndDist (wolf-compact-19), once the circling child was read.
struct BandRanges { float closeStart, closeEnd, leaveStart, leaveEnd; };
constexpr BandRanges kBattleBand{15, 9, 4, 11}, kHuntBand{10, 5, 1, 6};
// 通常行動 (TargetDistanceSelect, BoundaryDistance 11, OverlapDistance 2): the inner circle nearer than
// the boundary, the outer beyond. Reading the overlap as one unit either side is ours.
constexpr float kCircleBoundary = 11, kCircleOverlap = 2;
// Mod-authored: back in a fight this soon after the last one, the wolf skips the notice turn and roar
// (Clay, wolf-compact-18: the double bark repeated after the enemies were already seen).
constexpr float kRenotice = 300;
// Mod-authored: a wolf this far behind a following player takes up nothing new until back beside him
// (Clay, wolf-compact-18: he got distracted before reaching Link, and once led toward the sensor target).
constexpr float kCatchUpStart = 15, kCatchUpEnd = 6;
// A wolf that has stopped, at a follow point short of the player, is no longer catching up.
constexpr float kCatchUpStill = 30;
constexpr float kBarkChance = 0.055f, kHitReach = 6, kFoodReach = 1.2f, kFoodHeight = 0.5f;
constexpr float kHitTicks = 10, kStepLimit = 240, kChainTurnRate = 2;
// A warp that found no ground near the player is asked for again this much later, the wolf still hidden.
constexpr float kWarpAgain = 60;
// Follow failures that warp again soon after the last warp-in escalate a hidden wait, so ground the
// wolf cannot keep his feet on becomes a wait state instead of a loop of visible warps (Clay,
// wolf-compact-12). A whistle or a come order ends the wait at once.
constexpr float kQuickFailWindow = 300;
constexpr float kWarpHolds[]{0, 90, 240, 600};
// The original's timed reactions: a small hit is 5 to 7 ticks and a burn is 15 to 17.
constexpr float kSmallHit = 5, kBurnHit = 15, kHitSpread = 2;
// The original waits on the game alone; these only stop a stuck reading from parking the wolf.
constexpr float kFrozenLimit = 1800, kShockLimit = 300;
constexpr float kLeadArrive = 10, kLeadWait = 30, kLeadResume = 10, kLeadFace = 0.785f;
constexpr float kLeadGiveUp = 300, kLeadRetry = 900, kLeadLost = 30, kLeadProgress = 0.5f;
// OkPathFailRange: nearer than this a failed route is forgiven and tried again.
constexpr float kLeadPathFailRange = 60;
constexpr float kWallHitTicks = 45, kLostTicks = 25, kRestrictTicks = 300, kCloseLimit = 180;
// The original's battle path handling: a refused or abandoned route waits FailPathCooldownCounterLength
// before the next plan, and NoPathTimer of that waiting within one fight gives the enemy up.
// The hunting root's approach gives up sooner (NoPathTimer 120).
constexpr float kFailPathCooldown = 120, kNoPathTimer = 220, kHuntNoPathTimer = 120;
constexpr float kFoodTimeout = 300, kFoodPower = 3600;
// One meal per stop: after eating, further food is ignored for a while (Clay, wolf-compact-14).
constexpr float kFoodCooldown = 900;
// A hidden wait ends early once the player has clearly moved elsewhere since the warp-out.
constexpr float kHoldRelease = 10;
constexpr float kAfterHit = 90, kAfterHitRandom = 30;
constexpr float kFaceLeader = 1.745f, kFaceWarp = 0.96f, kFaceNear = 3;
constexpr float kCallDelay = 10, kCallSpeed = 11, kStillTicks = 10;
// A cutscene holds the wolf this long before it leaves until the cutscene ends.
constexpr float kEventPatience = 300;
constexpr float kFollowRetry = 30, kAlertTicks = 30;
// A wolf standing still this far from the player counts as lost even if no route reported it.
constexpr float kFollowFar = 17, kFollowStallTicks = 90;
// The original's head-turn timers: long stares while standing, short glances on the move.
constexpr float kLookRestStill = 30, kLookRestStillSpread = 15, kLookHoldStill = 5000, kLookHoldStillSpread = 40;
constexpr float kLookRestMoving = 150, kLookRestMovingSpread = 60, kLookHoldMoving = 10, kLookHoldMovingSpread = 40;
constexpr float kLookAtFood = 300;
constexpr int kBigHit = 8;
// The tag lasts one frame and can show just before the life drops, so it is held briefly.
constexpr std::uint8_t kStrongHit = 3;
constexpr float kHeavyHold = 4;
constexpr unsigned kFollowFailures = 2;
constexpr unsigned kChainAttacks = 5;

// A linear blend of the original angle limits by how close the wolf already is.
[[nodiscard]] constexpr float attackAngle(float distance, bool prey) {
    const float t = 1 - std::clamp(distance / kBeginAttack, 0.0f, 1.0f);
    const float low = prey ? 0.436f : 0.175f, high = prey ? 1.571f : 1.222f;
    return low + (high - low) * t;
}

// What a cutscene interrupts on the spot; scripted steps play out and reach it at their next choice.
[[nodiscard]] constexpr bool droppable(CompanionMode mode) {
    switch (mode) {
    case CompanionMode::Follow: case CompanionMode::Lead: case CompanionMode::WaitOrder: case CompanionMode::Lookout:
    case CompanionMode::Battle: case CompanionMode::Close: case CompanionMode::ChainClose: case CompanionMode::ToFood:
    case CompanionMode::NoPath: case CompanionMode::FollowRetry:
        return true;
    default:
        return false;
    }
}

// Engaged with an enemy, from the approach through the bite and its recovery.
[[nodiscard]] constexpr bool fighting(CompanionMode mode) {
    switch (mode) {
    case CompanionMode::Battle: case CompanionMode::Close: case CompanionMode::Strike: case CompanionMode::Recoil:
    case CompanionMode::ChainClose: case CompanionMode::ChainStrike: case CompanionMode::ChainEnd:
    case CompanionMode::AttackTurn:
        return true;
    default:
        return false;
    }
}

// Warping and summoning: damage is undone and the wolf reacts to nothing.
[[nodiscard]] constexpr bool warping(CompanionMode mode) {
    return mode == CompanionMode::Appear || mode == CompanionMode::WarpOut || mode == CompanionMode::WarpWait ||
           mode == CompanionMode::WarpIn;
}

// Idle scripts a leaving or far player pulls the wolf out of mid-animation: the notice and
// pre-attack turns, the look-around, the recoil hop, the howl and the bite. Strikes, reactions, warps
// and the summon-in keep playing (Clay, wolf-compact-15: he sat at a distraction until a whistle).
[[nodiscard]] constexpr bool yieldsToLeader(CompanionMode mode) {
    switch (mode) {
    case CompanionMode::Notice: case CompanionMode::Lookout: case CompanionMode::Recoil:
    case CompanionMode::ChainEnd: case CompanionMode::Facing: case CompanionMode::FollowRetry:
    case CompanionMode::Eat: case CompanionMode::AttackTurn:
        return true;
    default:
        return false;
    }
}
}

float Companion::roll() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return float(random_ % 65536u) / 65536.0f;
}

void Companion::attackGate() { gate_ = 30 + 30 * roll(); }

void Companion::returnToBattle() {
    attackGate();
    mode_ = CompanionMode::Battle;
}

void Companion::arrive(bool warped, int life) {
    if (warped) {
        sinceWarpIn_ = 0;
        script(CompanionMode::WarpIn, "WarpIn");
        return;
    }
    warpStreak_ = 0;
    sinceWarpIn_ = kQuickFailWindow;
    if (life >= 0) life_ = life < 1 ? kMaxLife : std::min(life, kMaxLife);
    script(CompanionMode::Appear, "Spawn", "Howling");
}

void Companion::script(CompanionMode mode, const char* first, const char* second, const char* third) {
    mode_ = mode;
    scriptAim_ = CompanionAim::None;
    steps_[0] = first;
    steps_[1] = second;
    steps_[2] = third;
    count_ = third ? 3 : second ? 2 : 1;
    step_ = 0;
    started_ = false;
    elapsed_ = 0;
    stepLimit_ = kStepLimit;
}

void Companion::knockedDown() { script(CompanionMode::Reaction, "Dead", "Standup"); }

void Companion::timed(float ticks, float spread) {
    script(CompanionMode::Reaction, "Damage");
    stepLimit_ = ticks + spread * roll();
}

// A turn that plays out before the state it announces starts.
void Companion::notice(CompanionMode next, CompanionAim aim, const char* first, const char* second) {
    script(CompanionMode::Notice, first, second);
    afterNotice_ = next;
    scriptAim_ = aim;
}

bool Companion::runScript(const CompanionInput& input, CompanionOutput& output) {
    output.move = CompanionMove::Script;
    output.aim = scriptAim_;
    if (started_) {
        elapsed_ += input.delta;
        if (input.animationDone || elapsed_ >= stepLimit_) {
            elapsed_ = 0;
            started_ = false;
            if (++step_ >= count_) return true;
        }
    }
    if (!started_) {
        output.play = steps_[step_];
        started_ = true;
        const std::string_view command(output.play);
        if (command == "Howling") alertTime_ = kAlertTicks;
        else if (command == "WarpOut") output.vanish = true;
        else if (command == "Spawn" || command == "WarpIn") output.appear = true;
    }
    // Every turn the original plays is an AnimalTurn, aimed again each tick; only the chain's pre-bite
    // turn is a plain animation (PlayASForAnimalUnit), at WolfLinkChainAttack's TurnAnimPlayRate 2.
    // Changed in wolf-compact-20 from aiming every turn once at its start, which let Turn_Attack play its
    // whole turn and overshoot (wolf-compact-19 log: before plus after summed to about 180 degrees).
    const bool chainTurn = mode_ == CompanionMode::AttackTurn && chainTurn_;
    output.steer = !chainTurn && scriptAim_ != CompanionAim::None &&
                   std::string_view(steps_[step_]).starts_with("Turn_");
    if (chainTurn) output.rate = kChainTurnRate;
    return false;
}

namespace {
// The game's count follows ours but never reaches zero, so the game runs no death of its own.
constexpr int shownLife(int life) { return std::max(life, 1); }
}

void Companion::observeLife(const CompanionInput& input, CompanionOutput& output) {
    heavyHit_ = input.status.hitStrength >= kStrongHit ? kHeavyHold : std::max(heavyHit_ - input.delta, 0.0f);
    if (life_ < 0) life_ = kMaxLife;
    if (input.life >= 0 && input.life != shownLife(life_)) output.setLife = shownLife(life_);
    if (input.hit <= 0 || dead() || invincible_ > 0 || warping(mode_) || mode_ == CompanionMode::Reaction) return;
    life_ = std::max(life_ - std::max(1, input.hit - companionDefense(kMaxLife)), 0);
    invincible_ = kAfterHit + kAfterHitRandom * roll();
    output.setLife = shownLife(life_);
    if (life_ == 0) {
        script(CompanionMode::Dying, "Dead", "Standup", "WarpOut");
        output.scatter = true;
    } else {
        hitReaction(input, input.hit);
    }
}

void Companion::hitReaction(const CompanionInput& input, int taken) {
    if (input.status.burning) timed(kBurnHit, kHitSpread);
    else if (input.status.gust || heavyHit_ > 0 || taken >= kBigHit) knockedDown();
    else timed(kSmallHit, kHitSpread);
}

// Same order as the original: ice, then shock, then whatever the element just did.
void Companion::observeStatus(const CompanionInput& input) {
    const bool burnStarted = input.status.burning && !burning_, gustStarted = input.status.gust && !gusted_;
    burning_ = input.status.burning;
    gusted_ = input.status.gust;
    if (!input.status.frozen && !input.status.shocked) statusSpent_ = false;
    if (!settled() && mode_ != CompanionMode::Reaction) return;
    if (mode_ == CompanionMode::Frozen || mode_ == CompanionMode::Shocked || statusSpent_) return;
    if (input.status.frozen) {
        mode_ = CompanionMode::Frozen;
        statusTime_ = 0;
        return;
    }
    if (input.status.shocked) {
        script(CompanionMode::Shocked, "Damage");
        statusTime_ = 0;
        return;
    }
    if (mode_ == CompanionMode::Reaction) return;
    if (burnStarted) timed(kBurnHit, kHitSpread);
    else if (gustStarted) knockedDown();
}

void Companion::recover(const CompanionInput& input, CompanionOutput& output) {
    if (input.target.present) script(CompanionMode::Recoil, "BackStep");
    else selectNormal(input, output);
}

void Companion::leaveStatus(const CompanionInput& input, CompanionOutput& output) {
    statusSpent_ = input.status.frozen || input.status.shocked;
    recover(input, output);
}

bool Companion::wantsWarp(const CompanionInput& input) const {
    return input.deepWater || input.playerDistance > kWarpDistance;
}

// The wolf faces the player before leaving, except in water, where it cannot turn.
void Companion::warpOut(const CompanionInput& input) {
    ordered_ = stayHeard_ = comeHeard_ = whistled_ = false;
    followFailures_ = 0;
    pendingWarpHold_ = 0;
    holdStart_ = input.playerDistance;
    if (!input.deepWater && input.leaderAngle > kFaceWarp) {
        script(CompanionMode::WarpOut, "Turn_Notice_NoGrowl", "WarpOut");
        scriptAim_ = CompanionAim::Leader;
    } else {
        script(CompanionMode::WarpOut, "WarpOut");
    }
}

// Putting the wolf away plays the fade-out first. A wolf already hidden for a warp just goes.
void Companion::leave(const CompanionInput& input, CompanionOutput& output) {
    leaving_ = true;
    if (mode_ == CompanionMode::WarpWait || mode_ == CompanionMode::Gone) {
        mode_ = CompanionMode::Gone;
        output.leave = true;
    } else if (mode_ != CompanionMode::WarpOut) {
        warpOut(input);
    }
}

bool Companion::settled() const {
    return !warping(mode_) && !dead() && mode_ != CompanionMode::Reaction && mode_ != CompanionMode::Frozen &&
           mode_ != CompanionMode::Shocked;
}

// The three waiting states ignore orders but, unlike a warp or a death, still feel the elements.
bool Companion::listening() const {
    return settled() && mode_ != CompanionMode::EventHold && mode_ != CompanionMode::NoPath &&
           mode_ != CompanionMode::FollowRetry;
}

void Companion::faceLeader(const CompanionInput& input, float angle) {
    if (input.leaderAngle > angle && input.playerDistance >= kFaceNear) {
        script(CompanionMode::Facing, "Turn_Notice_NoGrowl");
        scriptAim_ = CompanionAim::Leader;
    }
}

bool Companion::wantsFood(const CompanionInput& input) const {
    // Scavenging is idle charm (Clay, wolf-compact-14): one meal at a time, never at full health and
    // never while the player is walking away. A player-dropped gift is deliberate feeding, so it is
    // still taken at full health and while the player merely walks — only a sprinting-away player
    // overrides it.
    if (foodCooldown_ > 0 || input.leaderFleeing) return false;
    if (!input.food.dropped && (input.leaderLeaving || life_ >= kMaxLife)) return false;
    return input.food.present && input.food.distance <= kHealRange && input.food.height <= kFoodHeight &&
           input.playerDistance <= kLeashRange;
}

bool Companion::wantsFight(const CompanionInput& input) const {
    // A sprinting-away player takes the wolf with him; a walking-away player only forgoes new prey
    // hunts — enemies are still engaged (Clay, wolf-compact-14).
    if (input.leaderFleeing) return false;
    if (input.target.prey && input.leaderLeaving) return false;
    return input.target.present && input.target.distance <= kBattleRange && input.playerDistance <= kLeashRange;
}

// Cutscenes outrank everything the wolf is free to drop, and it waits in place, facing the player.
void Companion::holdForEvent(const CompanionInput& input, CompanionOutput& output) {
    leadWaiting_ = false;
    mode_ = CompanionMode::EventHold;
    output.move = CompanionMove::Hold;
    if (eventTime_ <= 0) {
        warpOut(input);
    } else {
        faceLeader(input, kFaceLeader);
    }
}

void Companion::leaveEvent(const CompanionInput& input, CompanionOutput& output) {
    if (ordered_) {
        mode_ = CompanionMode::WaitOrder;
        output.move = CompanionMove::Hold;
    } else {
        selectNormal(input, output);
    }
}

// One failure earns a turn and a second try; the next one sends the wolf round by warp.
void Companion::followLost(const CompanionInput& input) {
    followStall_ = 0;
    if (++followFailures_ >= kFollowFailures) {
        if (sinceWarpIn_ >= kQuickFailWindow) warpStreak_ = 0;
        // Ground the wolf cannot keep his feet on right next to the player is a wait, not a warp
        // onto Link (Clay, wolf-compact-14: stairs stuck him and resummoned him on the player).
        if (input.playerDistance <= kLeashBreak) warpStreak_ = std::max(warpStreak_, 1u);
        constexpr auto kLastHold = sizeof(kWarpHolds) / sizeof(kWarpHolds[0]) - 1;
        const float hold = kWarpHolds[std::min<std::size_t>(warpStreak_, kLastHold)];
        ++warpStreak_;
        warpOut(input);
        pendingWarpHold_ = hold;
        return;
    }
    script(CompanionMode::FollowRetry, "Turn_Notice_NoGrowl");
    scriptAim_ = CompanionAim::Leader;
    stepLimit_ = kFollowRetry;
}

void Companion::followUpdate(const CompanionInput& input, CompanionOutput& output) {
    // Mod-authored: a wolf standing short of a standing player is lost too, and his failures count until
    // he is back beside him (Clay, wolf-compact-19: he sat at the edge of the catch-up range and did not
    // close in once Link stopped). Changed in wolf-compact-20 from counting only beyond kFollowFar.
    const bool parked = input.leaderStill && input.playerDistance > kCatchUpEnd;
    if (input.playerDistance <= kCatchUpEnd || (input.playerDistance <= kFollowFar && !parked)) followFailures_ = 0;
    const bool stalled = input.standing && (input.playerDistance > kFollowFar || parked);
    followStall_ = stalled ? followStall_ + input.delta : 0;
    if (input.routeFailed || followStall_ >= kFollowStallTicks) followLost(input);
    else selectNormal(input, output);
}

// The timers are the original's. What the head turns to is this mod's reading: the focus, else the player.
void Companion::updateLook(const CompanionInput& input, CompanionOutput& output) {
    const auto aim = [&] {
        switch (mode_) {
        case CompanionMode::Battle: case CompanionMode::Close: case CompanionMode::ChainClose:
            return input.target.present ? CompanionAim::Target : CompanionAim::None;
        case CompanionMode::ToFood:
            return input.food.present ? CompanionAim::Food : CompanionAim::None;
        case CompanionMode::Lead:
            return input.lead.present ? CompanionAim::Lead : CompanionAim::None;
        case CompanionMode::Follow: case CompanionMode::WaitOrder: case CompanionMode::EventHold:
        case CompanionMode::NoPath:
            return CompanionAim::Leader;
        default:
            return CompanionAim::None;
        }
    }();
    if (aim == CompanionAim::None) {
        looking_ = false;
        lookTime_ = std::max(lookTime_, kLookRestMoving);
        return;
    }
    const bool still = input.standing;
    if (looking_ && !still) lookTime_ = std::min(lookTime_, kLookHoldMoving + kLookHoldMovingSpread);
    lookTime_ -= input.delta;
    if (lookTime_ <= 0) {
        looking_ = !looking_;
        lookTime_ = looking_
            ? (still ? kLookHoldStill + kLookHoldStillSpread * roll() : kLookHoldMoving + kLookHoldMovingSpread * roll())
            : (still ? kLookRestStill + kLookRestStillSpread * roll() : kLookRestMoving + kLookRestMovingSpread * roll());
    }
    if (looking_) output.look = aim;
}

void Companion::heed(const CompanionInput& input) {
    if (!listening()) return;
    stayHeard_ |= input.stay;
    comeHeard_ |= input.come;
    if (input.whistle) {
        whistled_ = true;
        callDelay_ = kCallDelay + input.playerDistance / kCallSpeed;
    }
}

void Companion::follow(CompanionOutput& output) {
    mode_ = CompanionMode::Follow;
    output.move = CompanionMove::Follow;
}

// Orders outrank fights and food, but a warp comes first and a notice turn always finishes.
bool Companion::obey(const CompanionInput& input, CompanionOutput& output) {
    if (!listening() || mode_ == CompanionMode::Notice || wantsWarp(input)) return false;
    const bool called = whistled_ && callDelay_ <= 0;
    if (comeHeard_ || called) {
        comeHeard_ = whistled_ = ordered_ = false;
        leadRetry_ = kLeadRetry;
        if (called && (mode_ != CompanionMode::Follow || input.standing))
            notice(CompanionMode::Follow, CompanionAim::Leader, "Turn_Notice_NoGrowl");
        else
            follow(output);
        return true;
    }
    if (stayHeard_) {
        stayHeard_ = false;
        ordered_ = true;
        mode_ = CompanionMode::WaitOrder;
        output.move = CompanionMove::Hold;
        return true;
    }
    return false;
}

void Companion::waitOrder(const CompanionInput& input, CompanionOutput& output) {
    output.move = CompanionMove::Hold;
    if (wantsWarp(input)) {
        warpOut(input);
    } else {
        faceLeader(input, kFaceLeader);
    }
}

void Companion::endLead(CompanionOutput& output) {
    leadRetry_ = kLeadRetry;
    leadWaiting_ = false;
    follow(output);
}

// The wolf walks on towards the sensor's target and waits for a player who falls behind, unless
// the player is already nearer the target than the wolf is.
void Companion::lead(const CompanionInput& input, CompanionOutput& output) {
    if (mode_ != CompanionMode::Lead) {
        mode_ = CompanionMode::Lead;
        leadBest_ = input.lead.present ? input.lead.distance : 0;
        leadStall_ = leadLost_ = 0;
    }
    output.move = CompanionMove::Lead;
    if (!input.lead.present) {
        leadLost_ += input.delta;
        if (leadLost_ >= kLeadLost) endLead(output);
        return;
    }
    leadLost_ = 0;
    if (input.lead.distance <= kLeadArrive ||
        (input.routeFailed && input.lead.distance > kLeadPathFailRange)) {
        endLead(output);
        return;
    }
    const bool ahead = input.lead.leaderDistance < input.lead.distance;
    if (leadWaiting_) leadWaiting_ = !ahead && input.playerDistance > kLeadResume;
    else leadWaiting_ = !ahead && input.playerDistance >= kLeadWait;
    if (leadWaiting_) {
        output.move = CompanionMove::Hold;
        faceLeader(input, kLeadFace);
        return;
    }
    if (input.lead.distance < leadBest_ - kLeadProgress) {
        leadBest_ = input.lead.distance;
        leadStall_ = 0;
    } else {
        leadStall_ += input.delta;
        if (leadStall_ >= kLeadGiveUp) endLead(output);
    }
}

void Companion::selectNormal(const CompanionInput& input, CompanionOutput& output) {
    if (input.event) {
        holdForEvent(input, output);
        return;
    }
    ordered_ = false;
    if (wantsWarp(input)) {
        warpOut(input);
        return;
    }
    if (mode_ == CompanionMode::Follow && input.noPath) {
        mode_ = CompanionMode::NoPath;
        output.move = CompanionMove::Hold;
        return;
    }
    if (!catchingUp_ && wantsFood(input)) {
        moving_ = 0;
        notice(CompanionMode::ToFood, CompanionAim::Food, "Turn_Notice_NoGrowl");
        return;
    }
    if (!catchingUp_ && wantsFight(input)) {
        wasPrey_ = input.target.prey;
        routeWait_ = noPath_ = 0;
        band_ = Band::Approach;
        if (sinceFight_ < kRenotice) {
            returnToBattle();
            battle(input, output);
        } else if (wasPrey_) {
            notice(CompanionMode::Battle, CompanionAim::Target, "Turn_Notice_NoGrowl");
        } else {
            notice(CompanionMode::Battle, CompanionAim::Target, "Turn_Attack", "Roar");
        }
        return;
    }
    if (mode_ == CompanionMode::Lead || (!catchingUp_ && input.lead.present && leadRetry_ <= 0)) {
        lead(input, output);
        return;
    }
    if (mode_ != CompanionMode::Follow) followStall_ = 0;
    follow(output);
    if (still_ >= kStillTicks) faceLeader(input, kFaceLeader);
}

void Companion::battle(const CompanionInput& input, CompanionOutput& output) {
    if (wantsWarp(input)) {
        warpOut(input);
        return;
    }
    if (input.playerDistance > kLeashBreak || input.leaderFleeing) {
        if (input.target.present) giveUp(output, input.leaderFleeing ? "flee" : "leash");
        follow(output);
        return;
    }
    if (!input.target.present) {
        script(CompanionMode::Lookout, "Look");
        return;
    }
    if (input.target.height > kHeightLimit) {
        low_ += input.delta;
        if (low_ >= kLostTicks) {
            low_ = 0;
            giveUp(output, "height");
        }
    } else {
        low_ = 0;
    }
    if (waitForRoute(input, output)) return;
    gate_ -= input.delta;
    keepBand(input, output);
    if (gate_ > 0) return;
    const float range = input.target.prey ? kInitiateHunt : kInitiateBattle;
    if (input.target.distance >= range) {
        // ChanceToBarkOnAttackFail: a flat chance per failed attack check, then the same Turn_Attack + Roar
        // as the notice; the hunting root's chance is 0. Changed in wolf-compact-19 from a chance that grew
        // with every failure; -19 also dropped the turn, restored here after the original's sequence
        // (戦闘攻撃できない = Turn_Attack then Roar) was read.
        if (!input.target.prey && roll() < kBarkChance)
            notice(CompanionMode::Battle, CompanionAim::Target, "Turn_Attack", "Roar");
        attackGate();
        return;
    }
    stuck_ = 0;
    bestDistance_ = input.target.distance;
    moving_ = 0;
    if (charge_ >= 100 && life_ == kMaxLife) {
        chainHits_ = 0;
        mode_ = CompanionMode::ChainClose;
    } else {
        mode_ = CompanionMode::Close;
    }
}

// The original's transitions, in its order: approach until inside CloseEnd, leave until beyond LeaveEnd,
// and from the circle back to the approach beyond CloseStart or out to the leave inside LeaveStart.
void Companion::keepBand(const CompanionInput& input, CompanionOutput& output) {
    const auto& ranges = input.target.prey ? kHuntBand : kBattleBand;
    const float distance = input.target.distance;
    const auto enterCircle = [&] {
        band_ = Band::Circle;
        outerCircle_ = distance > kCircleBoundary;
    };
    if (band_ == Band::Approach && distance < ranges.closeEnd) enterCircle();
    if (band_ == Band::Leave && distance > ranges.leaveEnd) enterCircle();
    if (band_ == Band::Circle) {
        if (distance > ranges.closeStart) band_ = Band::Approach;
        else if (distance < ranges.leaveStart) band_ = Band::Leave;
        else if (distance > kCircleBoundary + kCircleOverlap / 2) outerCircle_ = true;
        else if (distance < kCircleBoundary - kCircleOverlap / 2) outerCircle_ = false;
    }
    if (band_ == Band::Approach) {
        output.move = CompanionMove::Chase;
        output.keptDistance = kKeepRange;
        return;
    }
    output.move = CompanionMove::Circle;
    output.circle = band_ == Band::Leave ? CircleBand::Leave : outerCircle_ ? CircleBand::Outer : CircleBand::Inner;
}

void Companion::strike(const CompanionInput& input, CompanionOutput& output, bool chain) {
    if (!input.target.present) {
        if (chain) {
            charge_ = std::max(0.0f, charge_ - float(chainHits_) * (100.0f / float(kChainAttacks)));
            script(CompanionMode::ChainEnd, "Howling");
        } else {
            returnToBattle();
        }
        return;
    }
    if (input.playerDistance > kLeashBreak || input.leaderFleeing) {
        if (chain) charge_ = std::max(0.0f, charge_ - float(chainHits_) * (100.0f / float(kChainAttacks)));
        giveUp(output, input.leaderFleeing ? "flee" : "leash");
        follow(output);
        return;
    }
    if (waitForRoute(input, output)) return;
    moving_ += input.delta;
    // Only standing still counts as a wall: a route bending round an obstacle gains no distance for
    // a while (changed in wolf-compact-17 from counting every tick without gain).
    if (input.target.distance < bestDistance_ - 0.5f) {
        bestDistance_ = input.target.distance;
        stuck_ = 0;
    } else if (input.standing) {
        stuck_ += input.delta;
    }
    if (stuck_ >= kWallHitTicks || moving_ >= kCloseLimit) {
        giveUp(output, stuck_ >= kWallHitTicks ? "wall" : "time");
        returnToBattle();
        output.move = CompanionMove::Hold;
        return;
    }
    if (input.target.distance > kBeginAttack) {
        output.move = CompanionMove::Chase;
        return;
    }
    if (input.target.angle > attackAngle(input.target.distance, input.target.prey)) {
        // In reach but facing away: the original turns on the spot with Turn_Attack, then bites.
        chainTurn_ = chain;
        script(CompanionMode::AttackTurn, "Turn_Attack");
        scriptAim_ = CompanionAim::Target;
        return;
    }
    bite(chain);
}

// Aimed, so the attack graph's lunge blend gets the real distance to the enemy (it was left unset).
void Companion::bite(bool chain) {
    struck_ = landed_ = false;
    if (chain) script(CompanionMode::ChainStrike, "Attack_ChainAttack");
    else script(CompanionMode::Strike, "Attack");
    scriptAim_ = CompanionAim::Target;
}

void Companion::giveUp(CompanionOutput& output, const char* reason) {
    output.rejectTarget = kRestrictTicks;
    output.gaveUp = reason;
}

// A refused or abandoned route: the wolf stands facing the enemy before the next plan, and too much
// waiting in one fight gives the enemy up.
bool Companion::waitForRoute(const CompanionInput& input, CompanionOutput& output) {
    if (input.routeFailed) routeWait_ = kFailPathCooldown;
    if (routeWait_ <= 0) return false;
    routeWait_ = std::max(0.0f, routeWait_ - input.delta);
    noPath_ += input.delta;
    output.move = CompanionMove::Hold;
    if (noPath_ >= (input.target.prey ? kHuntNoPathTimer : kNoPathTimer)) {
        routeWait_ = noPath_ = 0;
        giveUp(output, "route");
        returnToBattle();
    }
    return true;
}

void Companion::settleStrike(CompanionOutput& output) {
    if (landed_) noPath_ = 0;
    if (mode_ == CompanionMode::ChainStrike) {
        ++chainHits_;
        moving_ = stuck_ = 0;
        bestDistance_ = 1000;
        output.avoidTarget = true;
        if (chainHits_ >= kChainAttacks) {
            charge_ = std::max(0.0f, charge_ - 100.0f);
            script(CompanionMode::ChainEnd, "Howling");
        } else {
            mode_ = CompanionMode::ChainClose;
        }
        return;
    }
    attackGate();
    if (landed_) {
        charge_ = std::min(100.0f, charge_ + 7 + 3 * roll() + (fed() ? 10.0f : 0.0f));
        mode_ = CompanionMode::Battle;
    } else {
        script(CompanionMode::Recoil, "BackStep");
    }
}

CompanionOutput Companion::step(const CompanionInput& input) {
    CompanionOutput output;
    const float delta = input.delta;
    invincible_ = std::max(0.0f, invincible_ - delta);
    sinceWarpIn_ = std::min(sinceWarpIn_ + delta, kQuickFailWindow);
    fedTime_ = std::max(0.0f, fedTime_ - delta);
    foodCooldown_ = std::max(0.0f, foodCooldown_ - delta);
    callDelay_ = std::max(0.0f, callDelay_ - delta);
    leadRetry_ = std::max(0.0f, leadRetry_ - delta);
    still_ = input.standing ? still_ + delta : 0;
    if (input.playerDistance <= kCatchUpEnd || still_ >= kCatchUpStill) catchingUp_ = false;
    else if (mode_ == CompanionMode::Follow && input.playerDistance > kCatchUpStart) catchingUp_ = true;
    sinceFight_ = fighting(mode_) ? 0 : std::min(sinceFight_ + delta, kRenotice);
    alertTime_ = std::max(0.0f, alertTime_ - delta);
    if (input.event && !inEvent_) eventTime_ = kEventPatience;
    inEvent_ = input.event;
    if (mode_ == CompanionMode::EventHold) eventTime_ = std::max(0.0f, eventTime_ - delta);
    observeLife(input, output);
    observeStatus(input);
    heed(input);
    if (input.dismiss && !dead()) leave(input, output);
    if ((mode_ == CompanionMode::Strike || mode_ == CompanionMode::ChainStrike) && started_ && !struck_) {
        if (elapsed_ + delta >= kHitTicks || input.animationDone) {
            struck_ = true;
            landed_ = input.target.present && input.target.distance <= kHitReach;
            if (landed_) output.damage = companionPower(kMaxLife, fed()) - kNativeBitePower;
        }
    }
    if (input.event && droppable(mode_)) holdForEvent(input, output);
    else if (!obey(input, output)) advance(input, output);
    updateLook(input, output);
    output.alert = alertTime_ > 0;
    return output;
}

void Companion::finishNotice(const CompanionInput& input, CompanionOutput& output) {
    if (afterNotice_ == CompanionMode::Follow) {
        follow(output);
    } else if (afterNotice_ == CompanionMode::ToFood) {
        mode_ = CompanionMode::ToFood;
        output.move = CompanionMove::ToFood;
        if (looking_) lookTime_ = kLookAtFood;
    } else {
        returnToBattle();
        battle(input, output);
    }
}

void Companion::goToFood(const CompanionInput& input, CompanionOutput& output) {
    moving_ += input.delta;
    output.move = CompanionMove::ToFood;
    if (wantsWarp(input)) {
        warpOut(input);
    } else if (input.playerDistance > kLeashBreak || input.leaderFleeing) {
        output.rejectFood = true;
        follow(output);
    } else if (!input.food.present) {
        selectNormal(input, output);
    } else if (moving_ >= kFoodTimeout) {
        output.rejectFood = true;
        follow(output);
    } else if (input.food.distance <= kFoodReach) {
        eaten_ = input.food.recover;
        dropped_ = input.food.dropped;
        script(CompanionMode::Eat, "Eat");
    }
}

void Companion::finishEating(const CompanionInput& input, CompanionOutput& output) {
    output.eat = true;
    foodCooldown_ = kFoodCooldown;
    life_ = std::clamp(life_ + eaten_, 0, kMaxLife);
    output.setLife = life_;
    if (dropped_) {
        fedTime_ = kFoodPower;
        script(CompanionMode::ChainEnd, "Howling");
    } else {
        selectNormal(input, output);
    }
}

// During a cutscene the wolf stays gone and comes back when it ends. A warp with no ground to land
// on keeps him gone and asks again shortly; coming back at the old spot put him back in the water
// (wolf-compact-08).
void Companion::awaitWarp(const CompanionInput& input, CompanionOutput& output) {
    if (!warpSent_) {
        // A call, or a player who has clearly moved elsewhere since the warp-out, ends the hold.
        if (input.come || input.whistle || std::fabs(input.playerDistance - holdStart_) > kHoldRelease)
            warpAgain_ = 0;
        warpAgain_ = std::max(0.0f, warpAgain_ - input.delta);
        if (!input.event && warpAgain_ <= 0) warpSent_ = output.warp = true;
    } else if (input.warpFinished) {
        arrive(true);
    } else if (input.warpFailed) {
        warpSent_ = false;
        warpAgain_ = kWarpAgain;
    }
}

void Companion::advance(const CompanionInput& input, CompanionOutput& output) {
    if (warping(mode_) || mode_ == CompanionMode::Dying) invincible_ = 2;
    // A sprinting-away or past-the-leash player ends idle scripts at once instead of after the
    // animation; the normal selector then follows or warps (Clay, wolf-compact-15).
    if (yieldsToLeader(mode_) && !input.event &&
        (input.leaderFleeing || input.playerDistance > kLeashBreak || wantsWarp(input))) {
        selectNormal(input, output);
        return;
    }
    switch (mode_) {
    case CompanionMode::Appear:
    case CompanionMode::Lookout:
    case CompanionMode::WarpIn:
        if (runScript(input, output)) selectNormal(input, output);
        break;
    case CompanionMode::Follow:
        followUpdate(input, output);
        break;
    case CompanionMode::Lead:
        selectNormal(input, output);
        break;
    case CompanionMode::EventHold:
        output.move = CompanionMove::Hold;
        if (!input.event) leaveEvent(input, output);
        else holdForEvent(input, output);
        break;
    case CompanionMode::NoPath:
        output.move = CompanionMove::Hold;
        if (wantsWarp(input) || !input.noPath || wantsFood(input) || wantsFight(input)) selectNormal(input, output);
        break;
    case CompanionMode::FollowRetry:
        if (runScript(input, output)) follow(output);
        break;
    case CompanionMode::Notice:
        if (runScript(input, output)) finishNotice(input, output);
        break;
    case CompanionMode::WaitOrder:
        waitOrder(input, output);
        break;
    case CompanionMode::Facing:
        if (runScript(input, output)) {
            if (input.event) holdForEvent(input, output);
            else if (ordered_) mode_ = CompanionMode::WaitOrder;
            else selectNormal(input, output);
        }
        break;
    case CompanionMode::Battle:
        battle(input, output);
        break;
    case CompanionMode::Close:
        strike(input, output, false);
        break;
    case CompanionMode::ChainClose:
        strike(input, output, true);
        break;
    case CompanionMode::Strike:
    case CompanionMode::ChainStrike:
        if (runScript(input, output)) settleStrike(output);
        break;
    case CompanionMode::AttackTurn:
        // The bite follows only a turn that left him facing the enemy; otherwise he closes and turns again,
        // and the turns count toward the approach time limit. Changed in wolf-compact-19 from biting however
        // the turn ended (Clay, wolf-compact-18: he faced an enemy, turned away, and bit the wrong way).
        moving_ += input.delta;
        if (runScript(input, output)) {
            if (input.target.present && input.target.distance <= kBeginAttack &&
                input.target.angle <= attackAngle(input.target.distance, input.target.prey))
                bite(chainTurn_);
            else
                mode_ = chainTurn_ ? CompanionMode::ChainClose : CompanionMode::Close;
        }
        break;
    case CompanionMode::Recoil:
    case CompanionMode::ChainEnd:
        if (runScript(input, output)) returnToBattle();
        break;
    case CompanionMode::ToFood:
        goToFood(input, output);
        break;
    case CompanionMode::Eat:
        if (runScript(input, output)) finishEating(input, output);
        break;
    case CompanionMode::WarpOut:
        if (runScript(input, output)) {
            if (leaving_) {
                mode_ = CompanionMode::Gone;
                output.leave = true;
            } else {
                mode_ = CompanionMode::WarpWait;
                warpSent_ = false;
                warpAgain_ = pendingWarpHold_;
            }
        }
        break;
    case CompanionMode::WarpWait:
        awaitWarp(input, output);
        break;
    case CompanionMode::Reaction:
        if (runScript(input, output)) recover(input, output);
        break;
    case CompanionMode::Frozen:
        // No command at all: the ice stops the pose the wolf was already in.
        output.move = CompanionMove::Script;
        statusTime_ += input.delta;
        if (!input.status.frozen || statusTime_ >= kFrozenLimit) leaveStatus(input, output);
        break;
    case CompanionMode::Shocked:
        statusTime_ += input.delta;
        if (runScript(input, output)) {
            if (input.status.shocked && statusTime_ < kShockLimit) script(CompanionMode::Shocked, "Damage");
            else leaveStatus(input, output);
        }
        break;
    case CompanionMode::Dying:
        if (runScript(input, output)) {
            mode_ = CompanionMode::Gone;
            output.retire = true;
        }
        break;
    case CompanionMode::Gone:
        output.move = CompanionMove::Script;
        break;
    }
}

}
