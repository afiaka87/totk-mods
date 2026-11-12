// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>

namespace botw_abilities::wolf {

// Quarter hearts: BotW's top amiibo tier (range 12..80), twenty hearts, so 30-point swings take six hits.
inline constexpr int kMaxLife = 80;

[[nodiscard]] constexpr int companionPower(int maxLife, bool fed) {
    const int power = int(10.0f + 0.25f * float(maxLife / 4));
    return fed ? power * 5 : power;
}
[[nodiscard]] constexpr int companionDefense(int maxLife) { return int(3.5f + 0.6f * float(maxLife / 4)); }
// The bite's own hit lands this much with the enemy's flinch and hit sound (the actor's attack table,
// NATIVE_BITE_POWER in tools/build_wolf_actor.py); the code adds the rest of the power on a landed bite.
inline constexpr int kNativeBitePower = 1;

enum class CompanionMove : std::uint8_t { Script, Hold, Follow, Chase, ToFood, Lead, Circle };
// Which of the original's circling children a CompanionMove::Circle runs.
enum class CircleBand : std::uint8_t { None, Inner, Outer, Leave };
enum class CompanionAim : std::uint8_t { None, Target, Leader, Food, Lead };
enum class CompanionMode : std::uint8_t {
    Appear, Follow, Notice, Battle, Close, Strike, Recoil, ChainClose, ChainStrike, ChainEnd,
    ToFood, Eat, WarpOut, WarpWait, WarpIn, Reaction, Dying, Gone, WaitOrder, Facing, Lookout,
    Frozen, Shocked, Lead, EventHold, NoPath, FollowRetry, AttackTurn,
};

struct CompanionTarget { bool present{}, prey{}; float distance{}, angle{}, height{}; };
struct CompanionFood { bool present{}, dropped{}; float distance{}, height{}; int recover{}; };
// What the game's own elemental systems currently say about the wolf.
// hitStrength is the game's tag on this frame's hit: 0 none, then Weak 1, Moderate 2, Strong 3, Smash 4.
struct CompanionStatus { bool burning{}, frozen{}, shocked{}, gust{}; std::uint8_t hitStrength{}; };
// The Purah Pad sensor's current target, as flat distances from the wolf and from the player.
struct CompanionLead { bool present{}; float distance{}, leaderDistance{}; };

struct CompanionInput {
    float delta = 1;
    float playerDistance{};
    float leaderAngle{};  // Radians between the wolf's nose and the player.
    bool deepWater{}, animationDone{}, warpFinished{}, warpFailed{};
    bool standing{};  // The wolf is not travelling.
    bool event{};  // A cutscene or scripted event is playing.
    bool noPath{};  // No walkable ground near the player to path to.
    bool routeFailed{};  // A planned route gave up this tick.
    bool leaderLeaving{};  // The player is moving away from the wolf, past a small gap.
    bool leaderFleeing{};  // The player is sprinting away: every distraction yields to following.
    bool leaderStill{};  // The player is standing.
    bool stay{}, come{}, whistle{};  // True for the one tick the player gave the order.
    bool dismiss{};  // True for the one tick the player put the wolf away.
    int life = -1;  // The game's own count; negative when the actor has no readable life value.
    int hit{};  // Damage points kept from the game this tick, the player's own hits already removed.
    CompanionTarget target;
    CompanionFood food;
    CompanionStatus status;
    CompanionLead lead;
};

struct CompanionOutput {
    CompanionMove move = CompanionMove::Script;
    CircleBand circle = CircleBand::None;
    float keptDistance{};
    const char* play{};
    float rate = 1;
    CompanionAim aim = CompanionAim::None;
    bool steer{};  // The playing turn is aimed at `aim` again this tick, as the original's AnimalTurn does.
    bool warp{}, retire{}, eat{}, rejectFood{}, avoidTarget{};
    bool leave{};  // The fade-out for a dismiss has played; the actor may go.
    bool alert{};  // A howl is carrying: nearby enemies should hear it this tick.
    bool scatter{};  // Nearby animals should run from this spot.
    bool appear{};  // The materialize flash fires at the wolf this tick.
    bool vanish{};  // The dematerialize flash fires at the wolf this tick.
    CompanionAim look = CompanionAim::None;  // Where the head turns; None rests it.
    int setLife = -1, damage{};
    float rejectTarget{};  // Ticks to ignore the current target; zero leaves it selectable.
    const char* gaveUp{};  // Why the current target was dropped this tick, for the log.
};

// A sprint away counts once it has lasted kConfirm ticks, and stops counting after kRelease ticks without
// one or once the player is back within kNear. Changed in wolf-compact-19 from a single-tick reading,
// which flipped 31 times in one session; each flip back replayed the enemy notice (wolf-compact-18).
class FleeLatch {
public:
    static constexpr float kConfirm = 10, kRelease = 45, kNear = 5;
    bool update(bool sprintingAway, float playerDistance, float delta) {
        if (sprintingAway) {
            calm_ = 0;
            run_ += delta;
            if (run_ >= kConfirm) on_ = true;
        } else {
            run_ = 0;
            calm_ = on_ ? calm_ + delta : 0;
            if (calm_ >= kRelease || playerDistance <= kNear) on_ = false;
        }
        return on_;
    }
private:
    float run_{}, calm_{};
    bool on_{};
};

// One owner on the AI worker. It decides; the native adapter moves, animates and applies the output.
class Companion {
public:
    explicit Companion(std::uint32_t seed = 1) : random_(seed | 1) {}
    // A warp replacement keeps life and charge and arrives with WarpIn instead of the summon.
    // A same-day summon passes the life the wolf left with; negative means full.
    void arrive(bool warped, int life = -1);
    [[nodiscard]] CompanionOutput step(const CompanionInput& input);
    [[nodiscard]] CompanionMode mode() const { return mode_; }
    [[nodiscard]] int life() const { return life_; }
    [[nodiscard]] float charge() const { return charge_; }
    [[nodiscard]] bool fed() const { return fedTime_ > 0; }
    [[nodiscard]] bool dead() const { return mode_ == CompanionMode::Dying || mode_ == CompanionMode::Gone; }
    // True once the player has told the wolf to stay, until something ends the order.
    [[nodiscard]] bool ordered() const { return ordered_; }
    // The original throws orders away in these states rather than queueing them.
    [[nodiscard]] bool listening() const;
private:
    void advance(const CompanionInput& input, CompanionOutput& output);
    void finishNotice(const CompanionInput& input, CompanionOutput& output);
    void goToFood(const CompanionInput& input, CompanionOutput& output);
    void finishEating(const CompanionInput& input, CompanionOutput& output);
    void awaitWarp(const CompanionInput& input, CompanionOutput& output);
    [[nodiscard]] bool settled() const;
    [[nodiscard]] bool wantsFood(const CompanionInput& input) const;
    [[nodiscard]] bool wantsFight(const CompanionInput& input) const;
    void holdForEvent(const CompanionInput& input, CompanionOutput& output);
    void leaveEvent(const CompanionInput& input, CompanionOutput& output);
    void followLost(const CompanionInput& input);
    void followUpdate(const CompanionInput& input, CompanionOutput& output);
    void updateLook(const CompanionInput& input, CompanionOutput& output);
    void observeLife(const CompanionInput& input, CompanionOutput& output);
    void script(CompanionMode mode, const char* first, const char* second = nullptr, const char* third = nullptr);
    void notice(CompanionMode next, CompanionAim aim, const char* first, const char* second = nullptr);
    void heed(const CompanionInput& input);
    [[nodiscard]] bool obey(const CompanionInput& input, CompanionOutput& output);
    void timed(float ticks, float spread);
    void knockedDown();
    void recover(const CompanionInput& input, CompanionOutput& output);
    void faceLeader(const CompanionInput& input, float angle);
    void hitReaction(const CompanionInput& input, int taken);
    void observeStatus(const CompanionInput& input);
    void leaveStatus(const CompanionInput& input, CompanionOutput& output);
    void follow(CompanionOutput& output);
    void lead(const CompanionInput& input, CompanionOutput& output);
    void endLead(CompanionOutput& output);
    void waitOrder(const CompanionInput& input, CompanionOutput& output);
    [[nodiscard]] bool runScript(const CompanionInput& input, CompanionOutput& output);
    void selectNormal(const CompanionInput& input, CompanionOutput& output);
    void battle(const CompanionInput& input, CompanionOutput& output);
    void keepBand(const CompanionInput& input, CompanionOutput& output);
    void strike(const CompanionInput& input, CompanionOutput& output, bool chain);
    void bite(bool chain);
    void settleStrike(CompanionOutput& output);
    void giveUp(CompanionOutput& output, const char* reason);
    [[nodiscard]] bool waitForRoute(const CompanionInput& input, CompanionOutput& output);
    void attackGate();
    void returnToBattle();
    void warpOut(const CompanionInput& input);
    void leave(const CompanionInput& input, CompanionOutput& output);
    [[nodiscard]] bool wantsWarp(const CompanionInput& input) const;
    [[nodiscard]] float roll();

    // Where the wolf is in the original's battle band between attacks.
    enum class Band : std::uint8_t { Approach, Circle, Leave };

    const char* steps_[3]{};
    CompanionMode mode_ = CompanionMode::Appear, afterNotice_ = CompanionMode::Battle;
    Band band_ = Band::Approach;
    CompanionAim scriptAim_ = CompanionAim::None;
    unsigned step_{}, count_{}, chainHits_{};
    bool started_{}, struck_{}, landed_{}, wasPrey_{}, dropped_{}, chainTurn_{};
    bool ordered_{}, stayHeard_{}, comeHeard_{}, whistled_{}, burning_{}, gusted_{}, statusSpent_{}, leadWaiting_{};
    bool inEvent_{}, warpSent_{}, looking_{}, leaving_{}, outerCircle_{}, catchingUp_{};
    float elapsed_{}, stepLimit_{}, statusTime_{}, charge_{}, gate_{}, invincible_{}, fedTime_{}, warpAgain_{};
    float sinceWarpIn_ = 1e9f, sinceFight_ = 1e9f, pendingWarpHold_{}, foodCooldown_{}, holdStart_{};
    float low_{}, stuck_{}, bestDistance_{}, moving_{}, callDelay_{}, still_{}, routeWait_{}, noPath_{};
    float leadRetry_{}, leadBest_{}, leadStall_{}, leadLost_{};
    float eventTime_{}, followStall_{}, alertTime_{}, lookTime_{}, heavyHit_{};
    int life_ = -1, eaten_{};
    unsigned followFailures_{}, warpStreak_{};
    std::uint32_t random_;
};

}
