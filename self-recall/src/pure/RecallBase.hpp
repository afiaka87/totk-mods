#pragma once

#ifndef SELF_RECALL_STORAGE_PROFILE
#define SELF_RECALL_STORAGE_PROFILE 8
#endif

namespace self_recall::pure {

inline constexpr unsigned kMiB = 1024u * 1024u;

#if SELF_RECALL_STORAGE_PROFILE == 7
inline constexpr const char* kStorageProfileName = "switch-compressed";
inline constexpr unsigned kPosePayloadArenaBytes = 10u * kMiB;
inline constexpr unsigned kAppearancePoolBytes = 5u * kMiB / 4u;
inline constexpr unsigned kArchiveHeapBytes = 2u * kMiB;
#elif SELF_RECALL_STORAGE_PROFILE == 8
inline constexpr const char* kStorageProfileName = "emulator-compressed";
inline constexpr unsigned kPosePayloadArenaBytes = 72u * kMiB;
inline constexpr unsigned kAppearancePoolBytes = 16u * kMiB;
inline constexpr unsigned kArchiveHeapBytes = 16u * kMiB;
#else
#error "Unknown SELF_RECALL_STORAGE_PROFILE"
#endif

inline constexpr bool kReducedHistory = SELF_RECALL_STORAGE_PROFILE == 7;
inline constexpr unsigned kHistoryFrameCapacity = kReducedHistory ? 902u : 3840u;
inline constexpr unsigned kHistorySeconds = kReducedHistory ? 30u : 64u;

inline constexpr unsigned kAppearanceBlockBytes = 256u;
inline constexpr unsigned kAppearanceBlockCount = kAppearancePoolBytes / kAppearanceBlockBytes;
inline constexpr unsigned kAppearanceStateCapacity = kAppearanceBlockCount;

static_assert(kPosePayloadArenaBytes % 1024u == 0);
static_assert(kAppearancePoolBytes % kAppearanceBlockBytes == 0);
static_assert(kArchiveHeapBytes % 4096u == 0);

}

#include <cstdint>
namespace self_recall::pure {

constexpr std::uint16_t kHistoryCapacity = kHistoryFrameCapacity;

struct HoldState {
    std::uint8_t ticks = 0;
    bool fired = false;
};

constexpr bool holdStep(HoldState& state, bool held, std::uint8_t threshold) {
    if (!held) {
        state = {};
        return false;
    }
    if (state.fired) return false;
    if (state.ticks < threshold) ++state.ticks;
    if (state.ticks < threshold) return false;
    state.fired = true;
    return true;
}

constexpr bool isTeleportDiscontinuity(float distanceMeters,
                                       float thresholdMeters = 30.0f) {
    return distanceMeters > thresholdMeters;
}

constexpr float kEngineStepHz = 30.0f;

constexpr float kCorrectionBaseMeters = 0.75f;
constexpr int kBlockPersistTicks = 6;

constexpr float correctionAllowanceMeters(float recordedPathSpeed,
                                          float recordedEngineSpeed,
                                          float liveEngineSpeed) {
    float fastest = recordedPathSpeed;
    if (recordedEngineSpeed > fastest) fastest = recordedEngineSpeed;
    if (liveEngineSpeed > fastest) fastest = liveEngineSpeed;
    const float positive = fastest > 0.0f ? fastest : 0.0f;
    return kCorrectionBaseMeters + positive / kEngineStepHz;
}

}

#include <cmath>

#include "totk/core/Types.hpp"

namespace self_recall::pure {

using Pose = totk::core::WorldTransform;
static_assert(sizeof(Pose) == 48,
              "recorded pose format: 9 rotation floats then 3 position floats");

constexpr std::uint16_t kMinHistory = 90;

enum SampleFlag : std::uint8_t {
    SampleAdmissible = 1u << 0,
    SampleClimb = 1u << 1,
    SampleNativeGlide = 1u << 2,
    SampleControlStick = 1u << 3,
};

struct HistorySample {
    Pose pose;
    float engineVelocity[3];
    float pathSpeed;
    std::uint8_t flags;
};
static_assert(sizeof(HistorySample) == 68, "recorded sample size");

inline float distance3(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline float distance3(const totk::core::WorldPosition& a,
                       const totk::core::WorldPosition& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline float distance3(const totk::core::WorldPosition& a, const float b[3]) {
    const float dx = a.x - b[0];
    const float dy = a.y - b[1];
    const float dz = a.z - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline bool finitePose(const Pose& pose) {
    for (float value : pose.rotation.values)
        if (!std::isfinite(value)) return false;
    return std::isfinite(pose.position.x) && std::isfinite(pose.position.y) &&
           std::isfinite(pose.position.z);
}

inline std::int32_t yawMilliDegrees(const Pose& pose) {
    constexpr float kRadToMilliDegrees = 57295.7795f;
    const float yaw =
        std::atan2(-pose.rotation.values[2], -pose.rotation.values[8]);
    return std::isfinite(yaw)
               ? static_cast<std::int32_t>(yaw * kRadToMilliDegrees)
               : 0;
}

}

#include <array>

namespace self_recall::pure {
enum class PlaybackRate : std::uint8_t { Normal = 4, X125 = 5, X150 = 6, X175 = 7, X200 = 8, X400 = 16 };
inline constexpr std::array kPlaybackRates{PlaybackRate::Normal, PlaybackRate::X125,
    PlaybackRate::X150, PlaybackRate::X175, PlaybackRate::X200, PlaybackRate::X400};
inline bool validPlaybackRate(PlaybackRate rate) {
    for (const auto candidate : kPlaybackRates) if (candidate == rate) return true;
    return false;
}
inline const char* playbackRateText(PlaybackRate rate) {
    switch (rate) {
        case PlaybackRate::Normal: return "1.00x";
        case PlaybackRate::X125: return "1.25x";
        case PlaybackRate::X150: return "1.50x";
        case PlaybackRate::X175: return "1.75x";
        case PlaybackRate::X200: return "2.00x";
        case PlaybackRate::X400: return "4.00x";
    }
    return "invalid";
}

}

namespace self_recall::pure {

inline constexpr std::uint64_t kButtonRightStick = 1ull << 5;
inline constexpr std::uint64_t kButtonZL = 1ull << 8;
inline constexpr std::uint64_t kActivationButtons = kButtonZL | kButtonRightStick;

[[nodiscard]] constexpr bool activationHeld(std::uint64_t buttons) {
    return (buttons & kActivationButtons) == kActivationButtons;
}

[[nodiscard]] constexpr std::uint64_t activationOwnedButtons(std::uint64_t buttons) {
    return activationHeld(buttons) ? kButtonRightStick : 0;
}

}

#include <algorithm>
#include <initializer_list>
namespace self_recall::pure {
inline bool motionDiscontinuity(totk::core::WorldPosition from,
        totk::core::WorldPosition to, const float previousVelocity[3],
        const float currentVelocity[3], double seconds, float threshold) {
    if (!isTeleportDiscontinuity(distance3(from, to), threshold)) return false;
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > 1) return true;
    for (const auto* velocity : {previousVelocity, currentVelocity}) {
        if (!velocity) continue;
        double travel[3], length2 = 0, dot = 0;
        const double delta[]{to.x - from.x, to.y - from.y, to.z - from.z};
        bool valid = true;
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(velocity[i])) { valid = false; break; }
            travel[i] = velocity[i] * seconds;
            length2 += travel[i] * travel[i];
            dot += delta[i] * travel[i];
        }
        if (!valid || !length2) continue;
        const auto fraction = std::clamp(dot / length2, 0.0, 1.0);
        double error2 = 0;
        for (int i = 0; i < 3; ++i) {
            const auto error = delta[i] - travel[i] * fraction;
            error2 += error * error;
        }
        if (error2 <= static_cast<double>(threshold) * threshold) return false;
    }
    return true;
}
}

namespace self_recall::pure {

struct OutfitSpeedProfile {
    const char* label;
    std::array<const char*, 3> seriesBySlot;
    std::array<PlaybackRate, 4> ratesByCount;
};

inline constexpr OutfitSpeedProfile kRecallOutfitSpeed{
    "Glide set", {"Diving", "Diving", "Diving"},
    {PlaybackRate::X125, PlaybackRate::X150, PlaybackRate::X200, PlaybackRate::X400}};

struct OutfitSnapshot {
    std::uint8_t matchedSlots = 0;
    std::array<std::uint32_t, 3> actorIds{};
};

class OutfitSpeed {
    const OutfitSpeedProfile& profile_;
    OutfitSnapshot outfit_{};
    bool initialized_ = false;
public:
    explicit OutfitSpeed(const OutfitSpeedProfile& profile = kRecallOutfitSpeed) : profile_(profile) {}
    unsigned count() const {
        unsigned result = 0;
        for (unsigned slot = 0; slot < 3; ++slot)
            if (outfit_.matchedSlots & (1u << slot)) ++result;
        return result;
    }
    PlaybackRate rate() const { return profile_.ratesByCount[count()]; }
    std::uint8_t mask() const { return outfit_.matchedSlots; }
    bool update(OutfitSnapshot next) {
        next.matchedSlots &= 7;
        for (unsigned slot = 0; slot < 3; ++slot) {
            if (!profile_.seriesBySlot[slot]) next.matchedSlots &= ~(1u << slot);
            if (!(next.matchedSlots & (1u << slot))) next.actorIds[slot] = 0;
        }
        const bool changed = !initialized_ || next.matchedSlots != outfit_.matchedSlots ||
            next.actorIds != outfit_.actorIds;
        outfit_ = next;
        initialized_ = true;
        return changed;
    }
    void reset() { outfit_ = {}; initialized_ = false; }
};
}

#include <atomic>
namespace self_recall::pure {
class NativeVehicleState {
public:
    void enter(std::uint32_t actorId) {
        if (actorId) actor_.store(actorId, std::memory_order_release);
    }
    void leave(std::uint32_t actorId) {
        if (actorId) actor_.compare_exchange_strong(actorId, 0, std::memory_order_acq_rel);
    }
    bool active(std::uint32_t actorId) const {
        return actorId && actor_.load(std::memory_order_acquire) == actorId;
    }
    void clear() { actor_.store(0, std::memory_order_release); }
private:
    std::atomic<std::uint32_t> actor_{0};
};

inline std::uint8_t pairVehicleAdmission(std::uint8_t flags, bool mayAdmit, bool nativeActive) {
    if (mayAdmit && (nativeActive || (flags & SampleControlStick)))
        flags |= SampleControlStick | SampleAdmissible;
    return flags;
}
}

namespace self_recall::pure {
struct ControllerPoseOutput {
    float* matrix = nullptr;
    std::array<float*, 4> velocities{};

    bool playerCommit(std::uintptr_t player) const {
        if (!player || !matrix) return false;
        for (unsigned i = 0; i < velocities.size(); ++i)
            if (reinterpret_cast<std::uintptr_t>(velocities[i]) != player + 0x320u + 12u * i)
                return false;
        return true;
    }

    bool apply(const Pose& pose) const {
        if (!matrix || !finitePose(pose)) return false;
        for (const auto* velocity : velocities) if (!velocity) return false;
        const float position[]{pose.position.x, pose.position.y, pose.position.z};
        for (unsigned row = 0; row < 3; ++row) {
            for (unsigned column = 0; column < 3; ++column)
                matrix[row * 4 + column] = pose.rotation.values[row * 3 + column];
            matrix[row * 4 + 3] = position[row];
        }
        for (auto* velocity : velocities)
            for (unsigned axis = 0; axis < 3; ++axis) velocity[axis] = 0;
        return true;
    }
};
}

#include <cstring>
namespace self_recall::pure {
struct AppliedClimb {
    std::uintptr_t player = 0;
    std::uint32_t actorId = 0, world = 0;
    HistorySample sample{};
};
class AppliedClimbMailbox {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static constexpr auto kWords = (sizeof(AppliedClimb) + 7) / 8;
    std::atomic<std::uint64_t> serial_{0};
    std::array<std::atomic<std::uint64_t>, kWords> words_{};
public:
    void publish(const AppliedClimb& value) {
        std::array<std::uint64_t, kWords> words{};
        std::memcpy(words.data(), &value, sizeof(value));
        serial_.fetch_add(1);
        for (unsigned i = 0; i < kWords; ++i) words_[i].store(words[i]);
        serial_.fetch_add(1);
    }
    bool snapshot(AppliedClimb& out) const {
        const auto serial = serial_.load();
        if (!serial || (serial & 1)) return false;
        std::array<std::uint64_t, kWords> words{};
        for (unsigned i = 0; i < kWords; ++i) words[i] = words_[i].load();
        if (serial_.load() != serial) return false;
        std::memcpy(static_cast<void*>(&out), words.data(), sizeof(out));
        return true;
    }
};
inline bool matchesAppliedPose(const AppliedClimb& applied, std::uintptr_t player,
                               std::uint32_t actorId, std::uint32_t world) {
    return player && world && applied.player == player && applied.actorId == actorId &&
           applied.world == world &&
           (applied.sample.flags & SampleAdmissible) && finitePose(applied.sample.pose);
}
}

namespace self_recall::pure {
enum class PlaybackStop {
    PoseApplyFailed, ClockUnavailable, RayBlocked, RouteUnavailable,
    NonfiniteCurrent, Blocked, Finished, UnsafeSample, PoseUnavailable,
    NonfiniteSample, NativePathFailed, PaletteFailed, AnimationRenderFailed,
    UnsafeLiveState, CancelledByB, StaminaExhausted, StaminaUnavailable, Count,
};

struct PlaybackStopText { const char* reason; const char* event; };

inline constexpr PlaybackStopText kPlaybackStopText[]{
    {"POSE_APPLY_FAILED", "stopped: player pose unavailable"},
    {"CLOCK_UNAVAILABLE", "stopped: gameplay clock unavailable"},
    {"RAY_BLOCKED", "path blocked - stopped in place"},
    {"ROUTE_UNAVAILABLE", "stopped: route check unavailable"},
    {"NONFINITE_CURRENT", "stopped: pose became invalid"},
    {"BLOCKED", "path blocked - stopped in place"},
    {"FINISHED", "route fully rewound"},
    {"UNSAFE_SAMPLE", "unsafe move reached - stopped in place"},
    {"POSE_UNAVAILABLE", "stopped: recorded animation unavailable"},
    {"NONFINITE_SAMPLE", "bad sample - stopped in place"},
    {"NATIVE_PATH_FAILED", "stopped: Recall path unavailable"},
    {"PALETTE_FAILED", "stopped: Recall colors unavailable"},
    {"ANIMATION_RENDER_FAILED", "stopped: recorded animation unavailable"},
    {"UNSAFE_LIVE_STATE", "another move took over - stopped in place"},
    {"CANCELLED_BY_B", "cancelled"},
    {"STAMINA_EXHAUSTED", "Recall ended: stamina depleted"},
    {"STAMINA_UNAVAILABLE", "stopped: stamina unavailable"},
};

static_assert(sizeof(kPlaybackStopText) / sizeof(*kPlaybackStopText) ==
              static_cast<unsigned>(PlaybackStop::Count));

constexpr PlaybackStopText playbackStopText(PlaybackStop stop) {
    const auto index = static_cast<unsigned>(stop);
    return index < static_cast<unsigned>(PlaybackStop::Count)
               ? kPlaybackStopText[index]
               : PlaybackStopText{"STOP", ""};
}

struct PlaybackExitPlan {
    bool releaseGlider = false;
};

constexpr PlaybackExitPlan playbackExitPlan(PlaybackStop stop, bool liveStateSafe,
                                           std::uint8_t lastSampleFlags) {
    const bool releasable = stop == PlaybackStop::Finished ||
                            stop == PlaybackStop::CancelledByB;
    return {releasable && liveStateSafe &&
            (lastSampleFlags & SampleNativeGlide) &&
            (lastSampleFlags & SampleAdmissible)};
}
}

namespace self_recall::pure {
inline constexpr float kRecallStaminaPerSecond = 28.0f;
inline constexpr std::uint64_t kRecallReleaseProtectionNs = 200000000;
enum class StaminaStatus { Unavailable, Empty, Available };
inline StaminaStatus staminaStatus(float normal, float bonus) {
    if (!std::isfinite(normal) || !std::isfinite(bonus) || normal < 0 || bonus < 0)
        return StaminaStatus::Unavailable;
    return normal > 0 || bonus > 0 ? StaminaStatus::Available : StaminaStatus::Empty;
}
inline bool releaseProtectionActive(std::uint64_t now, std::uint64_t until) {
    return until && now < until && until - now <= kRecallReleaseProtectionNs;
}
}

#include <limits>
namespace self_recall::pure {

enum class GameTimeStatus : std::uint8_t {
    Unavailable, Running, Paused, NoAdvance, InvalidDelta, Exhausted,
};

struct GameTimeSnapshot {
    std::uint64_t serial = 0;
    std::uint64_t elapsedNanoseconds = 0;
    std::uint32_t frameQuanta = 0;
    GameTimeStatus status = GameTimeStatus::Unavailable;
};

class GameTime {
public:
    GameTimeSnapshot update(float frameScale, bool paused, bool available = true) {
        if (snapshot_.status == GameTimeStatus::Exhausted || snapshot_.serial == UINT64_MAX) {
            snapshot_.status = GameTimeStatus::Exhausted;
            return snapshot_;
        }
        ++snapshot_.serial;
        snapshot_.frameQuanta = 0;
        if (!available) {
            snapshot_.status = GameTimeStatus::Unavailable;
            return snapshot_;
        }
        const double quanta = static_cast<double>(frameScale) * 2.0;
        if (!std::isfinite(quanta) || quanta < 0 || quanta > INT32_MAX ||
            quanta != std::floor(quanta)) {
            snapshot_.status = GameTimeStatus::InvalidDelta;
            return snapshot_;
        }
        snapshot_.frameQuanta = static_cast<std::uint32_t>(quanta);
        if (paused || !snapshot_.frameQuanta) {
            snapshot_.status = paused ? GameTimeStatus::Paused : GameTimeStatus::NoAdvance;
            return snapshot_;
        }
        const auto numerator = static_cast<std::uint64_t>(snapshot_.frameQuanta) *
                               1000000000ull + remainder_;
        const auto delta = numerator / 60;
        if (snapshot_.elapsedNanoseconds > UINT64_MAX - delta) {
            snapshot_.status = GameTimeStatus::Exhausted;
            return snapshot_;
        }
        snapshot_.elapsedNanoseconds += delta;
        remainder_ = static_cast<std::uint8_t>(numerator % 60);
        snapshot_.status = GameTimeStatus::Running;
        return snapshot_;
    }
private:
    GameTimeSnapshot snapshot_{};
    std::uint8_t remainder_ = 0;
};

inline constexpr std::uint64_t kRecallWindowNanoseconds = kHistorySeconds * 1000000000ull;
inline constexpr std::uint64_t kRecallMinimumNanoseconds = 1500000000ull;

}

#include <cstring>
#include <type_traits>

namespace self_recall::pure {

template <class T>
class FrameMailbox {
    static_assert(std::is_trivially_copyable_v<T>);
public:
    bool publish(const T& value) {
        if (busy_.test_and_set(std::memory_order_acquire)) return false;
        value_ = value;
        busy_.clear(std::memory_order_release);
        return true;
    }
    bool snapshot(T& out) {
        if (busy_.test_and_set(std::memory_order_acquire)) return false;
        out = value_;
        busy_.clear(std::memory_order_release);
        return true;
    }
private:
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    T value_{};
};

struct ActorFrameTicket {
    std::uint64_t actor = 0;
    std::uint64_t model = 0;
    std::uint32_t actorId = 0;
    std::uint32_t worldGeneration = 0;
    HistorySample route{};
    float modelRoot[12]{};
    float waterHeight = 0;
    bool haveWaterHeight = false;
};

inline bool matchesActorFrame(const ActorFrameTicket& ticket,
                              std::uint64_t actor, std::uint32_t actorId,
                              std::uint64_t model, std::uint32_t worldGeneration,
                              const Pose& pose, const float modelRoot[12]) {
    if (!modelRoot) return false;
    for (unsigned i = 0; i < 12; ++i)
        if (!std::isfinite(modelRoot[i])) return false;
    return actor && model && worldGeneration && modelRoot &&
           ticket.actor == actor && ticket.actorId == actorId &&
           ticket.model == model && ticket.worldGeneration == worldGeneration &&
           finitePose(pose) &&
           std::memcmp(&ticket.route.pose, &pose, sizeof(pose)) == 0 &&
           std::memcmp(ticket.modelRoot, modelRoot, sizeof(ticket.modelRoot)) == 0;
}

}

namespace self_recall::pure {

inline std::uint8_t pairNativeClimbAdmission(std::uint8_t flags, bool mayAdmit, bool nativeClimb) {
    return mayAdmit && nativeClimb
         ? static_cast<std::uint8_t>(flags | SampleClimb | SampleAdmissible) : flags;
}

class NativeTraversalState {
public:
    void enterFall(std::uint32_t actorId) { fall_.store(key(actorId)); }
    void enterGlide(std::uint32_t actorId) { glide_.store(key(actorId)); }
    void enterClimb(std::uint32_t actorId) { climb_.store(key(actorId)); }
    void leaveFall(std::uint32_t actorId) { auto expected = key(actorId); fall_.compare_exchange_strong(expected, 0); }
    void leaveGlide(std::uint32_t actorId) { auto expected = key(actorId); glide_.compare_exchange_strong(expected, 0); }
    void leaveClimb(std::uint32_t actorId) { auto expected = key(actorId); climb_.compare_exchange_strong(expected, 0); }
    bool falling(std::uint32_t actorId) const { return fall_.load() == key(actorId); }
    bool gliding(std::uint32_t actorId) const { return glide_.load() == key(actorId); }
    bool climbing(std::uint32_t actorId) const { return climb_.load() == key(actorId); }
    void clear() { fall_.store(0); glide_.store(0); climb_.store(0); }
private:
    static constexpr std::uint64_t key(std::uint32_t id) { return std::uint64_t{id} + 1; }
    std::atomic<std::uint64_t> fall_{0}, glide_{0}, climb_{0};
};

struct GliderReleaseContext {
    std::uintptr_t actor = 0;
    std::uint32_t actorId = 0;
    std::uint32_t worldGeneration = 0;
    std::uint64_t tick = 0;
    bool allowed = false;
};

enum class GliderReleaseEnd : unsigned {
    None, Entered, Cancelled, ContextChanged, TimedOut, NewRecall, Unavailable,
};

struct GliderReleaseResult {
    std::uint64_t serial = 0;
    GliderReleaseEnd reason = GliderReleaseEnd::None;
    std::uint32_t forcedCalls = 0;
};

class GliderRelease {
public:
    static constexpr std::uint64_t kAcquireTicks = 45;

    std::uint64_t arm(const GliderReleaseContext& context, bool recordedNativeGlide,
                      const NativeTraversalState& traversal) {
        cancel(GliderReleaseEnd::Cancelled);
        if (!recordedNativeGlide || !context.allowed || !context.actor || !context.worldGeneration ||
            traversal.gliding(context.actorId) || context.tick > UINT64_MAX - kAcquireTicks ||
            next_ == (UINT64_MAX >> 16)) return 0;
        Request request{context, ++next_};
        if (!request_.publish(request)) return 0;
        owned_ = request;
        now_.store(context.tick);
        forced_.store(request.serial << 16);
        active_.store(request.serial);
        return request.serial;
    }

    void service(const GliderReleaseContext& context, bool userCancelled) {
        now_.store(context.tick);
        const auto serial = active_.load();
        if (!serial) return;
        if (userCancelled) { finish(serial, GliderReleaseEnd::Cancelled); return; }
        const auto& started = owned_.context;
        if (owned_.serial != serial || !context.allowed || context.actor != started.actor ||
            context.actorId != started.actorId || context.worldGeneration != started.worldGeneration ||
            context.tick < started.tick) {
            finish(serial, GliderReleaseEnd::ContextChanged);
        } else if (context.tick - started.tick >= kAcquireTicks) {
            finish(serial, GliderReleaseEnd::TimedOut);
        }
    }

    std::uint64_t forceTicket(std::uintptr_t actor, std::uint32_t actorId,
                              const NativeTraversalState& traversal) {
        Request request;
        const auto serial = active_.load();
        if (!serial || !request_.snapshot(request) || request.serial != serial ||
            request.context.actor != actor || request.context.actorId != actorId ||
            !traversal.falling(actorId) || traversal.gliding(actorId)) return 0;
        const auto now = now_.load();
        if (now < request.context.tick || now - request.context.tick >= kAcquireTicks ||
            active_.load() != serial) return 0;
        return serial;
    }

    bool confirmForce(std::uint64_t serial) {
        auto value = forced_.load();
        while (serial && active_.load() == serial && (value >> 16) == serial) {
            if ((value & 0xFFFFu) == 0xFFFFu || forced_.compare_exchange_weak(value, value + 1))
                return active_.load() == serial;
        }
        return false;
    }

    void entered(std::uintptr_t actor, std::uint32_t actorId) {
        Request request;
        const auto serial = active_.load();
        if (serial && request_.snapshot(request) && request.serial == serial &&
            request.context.actor == actor && request.context.actorId == actorId)
            finish(serial, GliderReleaseEnd::Entered);
    }

    void cancel(GliderReleaseEnd reason = GliderReleaseEnd::Cancelled) { finish(active_.load(), reason); }
    void cancelTicket(std::uint64_t serial, GliderReleaseEnd reason) { finish(serial, reason); }
    GliderReleaseResult result() const {
        const auto terminal = terminal_.load();
        const auto serial = terminal >> 8;
        const auto forced = forced_.load();
        return {serial, static_cast<GliderReleaseEnd>(terminal & 0xFFu),
                (forced >> 16) == serial ? static_cast<std::uint32_t>(forced & 0xFFFFu) : 0};
    }

private:
    struct Request { GliderReleaseContext context{}; std::uint64_t serial = 0; };
    void finish(std::uint64_t serial, GliderReleaseEnd reason) {
        if (!serial || !active_.compare_exchange_strong(serial, 0)) return;
        const auto terminal = (serial << 8) | static_cast<unsigned>(reason);
        auto prior = terminal_.load();
        while (prior < terminal && !terminal_.compare_exchange_weak(prior, terminal)) {}
    }
    FrameMailbox<Request> request_;
    Request owned_{};
    std::uint64_t next_ = 0;
    std::atomic<std::uint64_t> active_{0}, now_{0}, forced_{0}, terminal_{0};
};

}
