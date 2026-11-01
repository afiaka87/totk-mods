// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "feature/Descend.hpp"

#include <lib.hpp>

#include "engine/Game.hpp"
#include "program/loggers.hpp"
#include "pure/DescendGeometry.hpp"
#include "pure/Proximity.hpp"
#include "totk/engine/Npad.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace descend::feature {
namespace {

namespace off = engine::offsets;
using engine::readMemory;
using engine::readVec;
using engine::ticks;
using engine::writeMemory;
using engine::writeVec;
using pure::Vec3;

constexpr std::uint32_t kSentinel = 0x7FC0DE5Cu; // quiet NaN no position ever holds
constexpr std::uint64_t kStickClick = 1u << 4;   // left stick press
constexpr std::uint64_t kRecent = ticks(100), kModeLapse = ticks(500), kArmedLife = ticks(1000),
                        kEventIdle = ticks(3000), kPauseGap = ticks(1000), kSettled = ticks(600);
constexpr int kMaxCameras = 4;

class SpinLock {
public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
        }
    }
    bool tryLock() { return !flag_.test_and_set(std::memory_order_acquire); }
    void unlock() { flag_.clear(std::memory_order_release); }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

class Hold {
public:
    explicit Hold(SpinLock& lock) : lock_(lock) { lock_.lock(); }
    ~Hold() { lock_.unlock(); }

private:
    SpinLock& lock_;
};

// The newest valid Descend target, published while aiming and consumed when an event starts.
struct Armed {
    bool valid = false;
    Vec3 origin{};
    Vec3 target{};
    float underside = 0;
    Vec3 drop{};
    std::uint64_t tick = 0;
    float floor = 0;
    std::uint8_t openViews = 0;
    int rockVotes = -1;
    engine::VoxelUp voxel{};
    float landing = NAN;
    pure::LandingFace landingFace = pure::LandingFace::None;
};

// The ceiling-clipper event this mod owns, from the selector's first frame through completion and camera handback. The travel stage is matched by its target rather than by time, so a pause cannot drop the event. The tick fields are the camera's timeline (zero: not yet).
struct Event {
    bool active = false;
    bool dropped = false;
    std::uint32_t serial = 0;
    std::uintptr_t actor = 0, player = 0, selector = 0;
    Vec3 target{};
    float underside = 0;
    Vec3 drop{};
    Vec3 start{};
    std::uint64_t touched = 0;
    float floor = 0;
    std::uint8_t openViews = 0;
    std::uint64_t startTick = 0, previewTick = 0, leaveTick = 0, cancelTick = 0;
    std::uint64_t fallTick = 0; // first cancel-stage update after A
    bool released = false, goalLogged = false;
    bool sinkPoseLogged = false;
    // Where the planner expected Link to land, and his lowest point after the drop's release.
    float landing = NAN;
    std::uint64_t releaseTick = 0, lowestTick = 0;
    float releaseY = 0, lowestY = 0;
    bool landLogged = false;
    Vec3 link{}; // published only by hooks receiving the live actor/node; render hooks never dereference actor
    pure::PreviewOrbit orbit{};
    pure::Pose previewPose{};
    bool previewReady = false;
    std::uint64_t orbitTick = 0;
};

// One camera the event drives. Each blends on its own, so a camera that joins late starts from its own view.
struct Camera {
    std::uintptr_t component = 0;
    pure::Phase phase = pure::Phase::Off;
    std::uint64_t phaseTick = 0;
    bool written = false, settledLogged = false;
    pure::Pose from{}, last{};
    unsigned writes = 0;
};

// The Descend aim view's inputs, published by the clipper update while Descend aim is on.
struct Aim {
    std::uintptr_t actor = 0;
    float reach = 1;
    std::uint64_t tick = 0;
    Vec3 link{};
};

// The aim view pose a camera last showed, so an event's first blend starts from what was on screen.
struct AimShown {
    std::uintptr_t component = 0;
    pure::Pose pose{};
    std::uint64_t tick = 0;
};

// The cameras of the current event, and the side chosen from the first one admitted.
struct CameraState {
    std::uint32_t serial = 0;
    int view = -1;
    Vec3 behind{};
    Camera cameras[kMaxCameras]{};
    int cameraCount = 0;
    bool skipLogged = false;
};

SpinLock g_lock;
Armed g_armed;
Event g_event;
// Taken only by the render-camera hook, which calls nothing in the game while holding it.
SpinLock g_cameraLock;
CameraState g_camera;
Aim g_aim;                    // g_lock
Vec3 g_aimDirection{0, 0, 1}; // the player's horizontal view while aiming, from the game camera; g_lock
float g_aimPitch = pure::tuning::kAimPitch; // the aim view's pitch above level; g_lock
float g_previewStickX = 0, g_previewStickY = 0; // g_lock
std::uint64_t g_previewStickTick = 0;
AimShown g_aimShown[kMaxCameras]{}; // g_cameraLock
pure::AimBlend g_aimBlend;          // g_cameraLock
float g_aimPitchStart = 0;          // the game camera's pitch when the aim view last turned on; g_cameraLock
bool g_fadeLogged = false;          // g_cameraLock
bool g_playerFadeLogged = false;    // g_cameraLock
std::atomic<std::uint64_t> g_aimTick{0};
// Read by the marker inline hook, which cannot inline std::atomic; accessed through __atomic builtins.
std::uint64_t g_markerTick = 0;
std::atomic<bool> g_descendMode{false};
std::uint32_t g_marker[9]{}; // cast start, cast end, miss position
pure::Proximity g_proximity;
std::uint32_t g_warm = 0, g_cold = 0x3F800000;
std::uintptr_t g_blendOwner = 0; // compared only with the live postCalc argument
// The player whose next cancel stage is the fall after A, and when A was pressed; read by the cancel-command inline hook through __atomic builtins. g_swirlSkipped is set when that hook replaced the swirl.
std::uintptr_t g_fallPlayer = 0;
std::uint64_t g_fallTick = 0;
std::uint32_t g_swirlSkipped = 0;
// The serial of the event this mod owns (0: none), and how often the in-ceiling screen effect was held off; read and written by the effect hook through __atomic builtins. g_effectLogged is the count already logged (g_cameraLock).
std::uint32_t g_ownedSerial = 0;
std::uint32_t g_effectHeld = 0;
std::uint32_t g_effectLogged = 0;
// 1 while the owned event moves Link through the slab, when the in-ceiling screen effect is shown, and how many effect updates showed it; read by the effect hook through __atomic builtins.
std::uint32_t g_swirlWanted = 0;
std::uint32_t g_effectShown = 0;
// How many updates found cover already raised, including our persistent cover from the previous update.
std::uint32_t g_effectNative = 0;
std::uint32_t g_returnEffect = 0;
std::uint64_t g_effectTick = 0;
// Compared with the current graphics owner before cleanup; never dereferenced after a scene change.
std::uintptr_t g_forcedCoverOwner = 0;
// Set while Descend aim is on; read by the prompt inline hooks through __atomic builtins.
std::uint32_t g_promptDescend = 0;
constexpr char16_t kDescendText[] = u"Descend";
totk::engine::NpadReader g_reader;
pure::ModeSwitch g_switch;
pure::Verdict g_loggedVerdict = pure::Verdict::Valid;
bool g_verdictLogged = false;

std::uint64_t now() { return svcGetSystemTick(); }

void forgetFall() { __atomic_store_n(&g_fallTick, 0, __ATOMIC_RELEASE); }

void setOwned(std::uint32_t serial) {
    if (!serial) {
        __atomic_store_n(&g_swirlWanted, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&g_returnEffect, 0, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&g_effectTick, serial ? now() : 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_ownedSerial, serial, __ATOMIC_RELEASE);
}

// Turns the in-ceiling screen effect on or off for the owned event, logging each change.
void setSwirl(std::uint32_t serial, bool on, const char* why) {
    const std::uint32_t wanted = on ? 1 : 0;
    if (__atomic_exchange_n(&g_swirlWanted, wanted, __ATOMIC_ACQ_REL) == wanted) return;
    Logging.Log("[descend] event %u in-ceiling screen effect %s: %s (%u cover updates, %u already set)",
                serial, on ? "on" : "off", why, __atomic_load_n(&g_effectShown, __ATOMIC_ACQUIRE),
                __atomic_load_n(&g_effectNative, __ATOMIC_ACQUIRE));
}

int mm(float value) { return std::isfinite(value) ? static_cast<int>(value * 1000) : -999999; }

int degrees(float radians) {
    return std::isfinite(radians) ? static_cast<int>(radians / pure::tuning::kRadiansPerDegree) : -999;
}

std::uint32_t bits(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

void publishMarker(Vec3 feet, std::uint64_t tick) {
    using namespace pure::tuning;
    const float values[9]{feet.x, feet.y + kMarkerAbove, feet.z, feet.x, feet.y - kMarkerBelow, feet.z,
                          feet.x, feet.y - kMarkerBelow, feet.z};
    for (int i = 0; i < 9; ++i) __atomic_store_n(&g_marker[i], bits(values[i]), __ATOMIC_RELAXED);
    __atomic_store_n(&g_markerTick, tick, __ATOMIC_RELEASE);
}

const char* voxelAnswer(const engine::VoxelUp& voxel) {
    switch (voxel.answer) {
    case engine::VoxelUp::Answer::NoModule: return "none";
    case engine::VoxelUp::Answer::Busy: return "busy";
    case engine::VoxelUp::Answer::NoUnit: return "no-unit";
    case engine::VoxelUp::Answer::Unit: return "unit";
    }
    return "unknown";
}

// Ascend's voxel look upward from a point: from the drop point the first solid should be the slab's underside.
void formatVoxel(char* out, std::size_t size, const engine::VoxelUp& voxel) {
    std::snprintf(out, size, "voxel=%s/%d first_solid_mm=%d solid_end_mm=%d next_solid_mm=%d", voxelAnswer(voxel),
                  voxel.found ? 1 : 0, mm(voxel.firstSolid), mm(voxel.solidEnd), mm(voxel.nextSolid));
}

// Two lines, as one would pass the log's 512-byte limit: the clipper-layer walk, then the solid-world checks.
void logVerdict(const pure::Plan& plan, Vec3 feet) {
    if (g_verdictLogged && plan.verdict == g_loggedVerdict) return;
    g_verdictLogged = true;
    g_loggedVerdict = plan.verdict;
    char voxelText[128], landingVoxelText[128];
    formatVoxel(voxelText, sizeof(voxelText), plan.voxel);
    formatVoxel(landingVoxelText, sizeof(landingVoxelText), plan.landingVoxel);
    const char* const verdict = pure::verdictName(plan.verdict);
    Logging.Log("[descend] target %s feet_mm=(%d,%d,%d) floor_mm=%d side1_mm=%d side2_mm=%d entry_side=%u "
                "crossings=%d underside_mm=%d thick_mm=%d clear_below_mm=%d rock_votes=%d views=0x%02x "
                "legs_views=0x%02x %s",
                verdict, mm(feet.x), mm(feet.y), mm(feet.z), mm(plan.floor), mm(plan.oneSideY), mm(plan.otherSideY),
                static_cast<unsigned>(plan.entry), plan.crossings, mm(plan.underside), mm(plan.thickness),
                mm(plan.clearBelow), plan.rockVotes, static_cast<unsigned>(plan.openViews),
                static_cast<unsigned>(plan.legsViews), voxelText);
    Logging.Log("[descend] target %s solid: rock_reason=%s solid_floor_mm=%d land_mm=%d land_face=%s land_votes=%d "
                "stray_mm=%d stray_kept=%s body_line=%d land_%s",
                verdict, pure::rockReasonName(plan.rockReason), mm(plan.solidFloor), mm(plan.landing),
                pure::landingFaceName(plan.landingFace), plan.landingVotes, mm(plan.stray),
                pure::strayKeptName(plan.strayKept), plan.bodyLine, landingVoxelText);
}

// Same flags the game sets when B is pressed during this stage, so the cancel stage takes over.
void requestCancel(std::uintptr_t player, std::uintptr_t selector) {
    using namespace off::player;
    const auto input = readMemory<std::uint32_t>(player + kCancelInputFlags);
    if (input & kCancelInput) writeMemory<std::uint32_t>(player + kCancelInputFlags, input & ~kCancelInput);
    writeMemory<std::uint32_t>(player + kEventFlags,
                               readMemory<std::uint32_t>(player + kEventFlags) | kCancelRequested);
    if (selector) writeMemory<std::uint8_t>(selector + off::selector::kCancelLatch, 0);
    if (readMemory<std::uint8_t>(player + kPreviewByte) & kPreviewBit)
        writeMemory<std::uint64_t>(player + kWaterCancelEffect,
                                   readMemory<std::uint64_t>(player + kWaterCancelEffect) | kWaterCancelBit);
}

// Decides whether the event that owns this actor's ceiling clipper is a Descend event.
bool resolve(std::uintptr_t actor, std::uintptr_t selector) {
    const auto clipper = engine::ceilingClipper(actor);
    const auto player = engine::playerComponent(actor);
    if (!clipper || !player) return false;
    const Vec3 committed = readVec(clipper + off::clipper::kCommitted);
    const auto tick = now();
    Event started{};
    Armed armed{};
    bool cleared = false;
    {
        Hold hold(g_lock);
        if (g_armed.valid && tick - g_armed.tick < kArmedLife && pure::sameBits(committed, g_armed.target)) {
            Vec3 start = readVec(player + off::player::kReturnTarget);
            if (!std::isfinite(start.x + start.y + start.z)) start = g_armed.origin;
            g_event = Event{true,           false,             g_event.serial + 1, actor, player, selector,
                            g_armed.target, g_armed.underside, g_armed.drop,       start, tick,
                            g_armed.floor,  g_armed.openViews, tick};
            g_event.landing = g_armed.landing;
            g_event.link = readVec(actor + off::actor::kPosition);
            const Vec3 behind{-g_aimDirection.x, 0, -g_aimDirection.z};
            g_event.previewPose = pure::previewPose(g_armed.target, g_armed.underside,
                                                    pure::chooseView(g_armed.openViews, behind), behind);
            g_event.orbit.begin(g_event.previewPose);
            armed = g_armed;
            g_armed = {};
            writeVec(player + off::player::kReturnTarget,
                     {start.x, start.y + pure::tuning::kReturnLift, start.z});
            started = g_event;
            __atomic_store_n(&g_effectShown, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&g_effectNative, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&g_swirlWanted, 1, __ATOMIC_RELEASE);
            setOwned(g_event.serial);
        } else if (g_event.active && g_event.actor == actor && pure::sameBits(committed, g_event.target) &&
                   tick - g_event.touched < kEventIdle) {
            if (selector) g_event.selector = selector;
            g_event.touched = tick;
            return true;
        } else if (g_event.active) {
            g_event.active = false;
            cleared = true;
            setOwned(0);
        }
    }
    if (cleared) {
        forgetFall();
        Logging.Log("[descend] event cleared: native ceiling target");
    }
    if (!started.active) return false;
    char voxelText[128];
    formatVoxel(voxelText, sizeof(voxelText), armed.voxel);
    Logging.Log("[descend] event %u start_mm=(%d,%d,%d) target_y_mm=%d underside_mm=%d drop_y_mm=%d floor_mm=%d "
                "rock_votes=%d land_mm=%d land_face=%s %s",
                started.serial, mm(started.start.x), mm(started.start.y), mm(started.start.z),
                mm(started.target.y), mm(started.underside), mm(started.drop.y), mm(started.floor), armed.rockVotes,
                mm(armed.landing), pure::landingFaceName(armed.landingFace), voxelText);
    Logging.Log("[descend] event %u in-ceiling screen effect on: sinking", started.serial);
    return true;
}

Vec3 readWide(std::uintptr_t address) {
    return {static_cast<float>(readMemory<double>(address)), static_cast<float>(readMemory<double>(address + 8)),
            static_cast<float>(readMemory<double>(address + 16))};
}

void writeWide(std::uintptr_t address, Vec3 value) {
    writeMemory<double>(address, value.x);
    writeMemory<double>(address + 8, value.y);
    writeMemory<double>(address + 16, value.z);
}

// The game uses an override when any of its three doubles is non-zero.
bool wideSet(std::uintptr_t address) {
    return readMemory<double>(address) != 0 || readMemory<double>(address + 8) != 0 ||
           readMemory<double>(address + 16) != 0;
}

// The pose the game would show this frame: its own override when one is set, otherwise the final pose.
pure::Pose nativePose(std::uintptr_t camera) {
    using namespace off::game_camera;
    return {readWide(camera + (wideSet(camera + kPositionOverride) ? kPositionOverride : kPosition)),
            readWide(camera + (wideSet(camera + kLookAtOverride) ? kLookAtOverride : kLookAt))};
}

bool finite(const pure::Pose& pose) {
    return std::isfinite(pose.position.x + pose.position.y + pose.position.z + pose.lookAt.x + pose.lookAt.y +
                         pose.lookAt.z);
}

std::int64_t milliseconds(std::uint64_t tick) { return static_cast<std::int64_t>(tick / ticks(1)); }

pure::Timeline timeline(const Event& event) {
    return {milliseconds(event.startTick), milliseconds(event.previewTick), milliseconds(event.leaveTick),
            milliseconds(event.cancelTick), milliseconds(event.touched),    event.dropped};
}

// Writes since the last line, the pose last written and the game's own pose; g_cameraLock held.
void logCamera(const Event& event, int index, const char* what, std::uint64_t tick, Vec3 link,
               const pure::Pose& native) {
    auto& camera = g_camera.cameras[index];
    Logging.Log("[descend] event %u camera %d %s at %u ms: writes=%u link_y_mm=%d pose_mm=(%d,%d,%d) "
                "look_mm=(%d,%d,%d) native_mm=(%d,%d,%d) look_mm=(%d,%d,%d)",
                event.serial, index, what, static_cast<unsigned>((tick - event.startTick) / ticks(1)),
                camera.writes, mm(link.y), mm(camera.last.position.x), mm(camera.last.position.y), mm(camera.last.position.z),
                mm(camera.last.lookAt.x), mm(camera.last.lookAt.y), mm(camera.last.lookAt.z), mm(native.position.x),
                mm(native.position.y), mm(native.position.z), mm(native.lookAt.x), mm(native.lookAt.y),
                mm(native.lookAt.z));
    camera.writes = 0;
}

// This frame's pose for one camera of the event, or false once it is handed back; g_cameraLock held.
bool framePose(const Event& event, int index, std::uint64_t tick, Vec3 link, const pure::Pose& native,
               pure::Pose& pose, bool& uncover) {
    auto& camera = g_camera.cameras[index];
    const auto phase = pure::phaseAt(timeline(event), milliseconds(tick));
    if (phase != camera.phase) {
        char what[32];
        std::snprintf(what, sizeof(what), "%s->%s", pure::phaseName(camera.phase), pure::phaseName(phase));
        logCamera(event, index, what, tick, link, native);
        camera.from = camera.written ? camera.last : native;
        camera.phase = phase;
        camera.phaseTick = tick;
        camera.settledLogged = false;
    }
    if (phase == pure::Phase::Off) return false;
    const pure::CameraScene scene{link, event.target, event.underside, g_camera.view, g_camera.behind, native};
    // All preview cameras share the same clock, so one cannot uncover another camera's unfinished blend.
    const auto blendStart = phase == pure::Phase::Preview ? event.previewTick : camera.phaseTick;
    const float weight = static_cast<float>(tick - blendStart) / static_cast<float>(ticks(pure::blendMs(phase)));
    auto destination = pure::phasePose(phase, scene);
    if (event.previewReady && phase == pure::Phase::Preview) destination = event.previewPose;
    if (event.previewReady && phase == pure::Phase::Drop)
        destination = {event.previewPose.position,
                       {link.x, std::fmin(event.previewPose.lookAt.y, link.y + pure::tuning::kTrackLook), link.z}};
    pose = pure::blend(camera.from, destination, weight);
    uncover = phase == pure::Phase::Preview && event.previewReady && weight >= 1.0f;
    camera.last = pose;
    camera.written = true;
    if (!camera.settledLogged && tick - camera.phaseTick >= kSettled) {
        camera.settledLogged = true;
        char what[32];
        std::snprintf(what, sizeof(what), "%s settled", pure::phaseName(phase));
        logCamera(event, index, what, tick, link, native);
    }
    return true;
}

// The camera's slot in this event, admitting it when its view is near Link; -1 when it is not driven. A camera that was showing the aim view blends from that view. g_cameraLock held.
int cameraIndex(const Event& event, std::uintptr_t component, Vec3 link, const pure::Pose& native,
                std::uint64_t tick) {
    auto& c = g_camera;
    if (c.serial != event.serial) {
        c = CameraState{};
        c.serial = event.serial;
    }
    for (int i = 0; i < c.cameraCount; ++i)
        if (c.cameras[i].component == component) return i;
    if (c.cameraCount == kMaxCameras || !pure::cameraInRange(native.position, link)) {
        if (!c.skipLogged) {
            c.skipLogged = true;
            Logging.Log("[descend] event %u camera %p skipped: %d cameras, native_mm=(%d,%d,%d)", event.serial,
                        reinterpret_cast<void*>(component), c.cameraCount, mm(native.position.x),
                        mm(native.position.y), mm(native.position.z));
        }
        return -1;
    }
    if (!c.cameraCount) {
        const Vec3 look = pure::horizontalDirection(native);
        c.behind = {-look.x, 0, -look.z};
        c.view = pure::chooseView(event.openViews, c.behind);
        Logging.Log("[descend] event %u camera view=%d views=0x%02x", event.serial, c.view,
                    static_cast<unsigned>(event.openViews));
    }
    const int index = c.cameraCount++;
    auto& camera = c.cameras[index] = Camera{component};
    for (const auto& shown : g_aimShown) {
        if (shown.component != component || !shown.tick || tick - shown.tick > kRecent) continue;
        camera.last = shown.pose;
        camera.written = true;
    }
    Logging.Log("[descend] event %u camera %d component=%p native_mm=(%d,%d,%d) look_mm=(%d,%d,%d) from_aim=%d",
                event.serial, index, reinterpret_cast<void*>(component), mm(native.position.x), mm(native.position.y),
                mm(native.position.z), mm(native.lookAt.x), mm(native.lookAt.y), mm(native.lookAt.z),
                camera.written ? 1 : 0);
    return index;
}

// Keeps the aim view pose this camera shows, replacing its own entry or the oldest; g_cameraLock held.
void rememberAimPose(std::uintptr_t component, const pure::Pose& pose, std::uint64_t tick) {
    int slot = 0;
    for (int i = 0; i < kMaxCameras; ++i) {
        if (g_aimShown[i].component == component) {
            slot = i;
            break;
        }
        if (g_aimShown[i].tick < g_aimShown[slot].tick) slot = i;
    }
    g_aimShown[slot] = {component, pose, tick};
}

// Writes this frame's pose through the camera's one-frame override, with a level up direction.
CameraUp showPose(std::uintptr_t component, const pure::Pose& pose) {
    Vec3 up;
    if (!pure::levelUp(pose, up)) return {};
    writeWide(component + off::game_camera::kPositionOverride, pose.position);
    writeWide(component + off::game_camera::kLookAtOverride, pose.lookAt);
    const CameraUp saved{true, readVec(component + off::game_camera::kUp)};
    writeVec(component + off::game_camera::kUp, up);
    return saved;
}

// The Descend aim view, while no event is owned: each camera near Link blends toward a view down at him while Descend aim is on, and back to its own view after. The game's aim camera keeps taking the right stick, so its turn and its change in pitch since the view turned on steer the view.
CameraUp aimCamera(std::uintptr_t component, const Aim& aim) {
    const auto tick = now();
    if (!aim.tick || tick - aim.tick > kRecent + ticks(pure::tuning::kAimBlendMs)) return {};
    const Vec3 link = aim.link;
    const auto native = nativePose(component);
    if (!finite(native) || !pure::cameraInRange(native.position, link)) return {};
    const bool on = tick - aim.tick < kRecent && g_descendMode.load(std::memory_order_acquire);
    const Vec3 direction = pure::horizontalDirection(native);
    const float nativePitch = pure::pitchUp(native);
    pure::Pose pose;
    float pitch;
    {
        Hold hold(g_cameraLock);
        const auto ms = milliseconds(tick);
        const bool turned = g_aimBlend.turn(on, ms);
        if (turned && on) {
            g_aimPitchStart = nativePitch;
            g_fadeLogged = false;
            g_playerFadeLogged = false;
        }
        pitch = pure::aimPitch(nativePitch, g_aimPitchStart);
        pose = pure::blend(native, pure::aimPose(link, direction, pitch, aim.reach), g_aimBlend.weight(ms));
        if (turned)
            Logging.Log("[descend] aim view %s: reach=%d%% pitch_deg=%d native_pitch_deg=%d link_mm=(%d,%d,%d) "
                        "native_mm=(%d,%d,%d) look_mm=(%d,%d,%d)",
                        on ? "on" : "off", static_cast<int>(aim.reach * 100), degrees(pitch), degrees(nativePitch),
                        mm(link.x), mm(link.y), mm(link.z), mm(native.position.x), mm(native.position.y),
                        mm(native.position.z), mm(native.lookAt.x), mm(native.lookAt.y), mm(native.lookAt.z));
        if (!(g_aimBlend.weight(ms) > 0) || !finite(pose)) return {};
        rememberAimPose(component, pose, tick);
    }
    if (on) {
        Hold hold(g_lock);
        g_aimDirection = direction;
        g_aimPitch = pitch;
    }
    return showPose(component, pose);
}

// The aim view pose this camera showed within the last kRecent, if any; g_cameraLock held.
const AimShown* recentAimPose(std::uintptr_t component, std::uint64_t tick) {
    for (const auto& shown : g_aimShown)
        if (shown.component == component && shown.tick && tick - shown.tick <= kRecent) return &shown;
    return nullptr;
}

// The newest aim view pose any camera showed within the last kRecent, if any; g_cameraLock held.
const AimShown* newestAimPose(std::uint64_t tick) {
    const AimShown* newest = nullptr;
    for (const auto& shown : g_aimShown)
        if (shown.tick && tick - shown.tick <= kRecent && (!newest || shown.tick > newest->tick)) newest = &shown;
    return newest;
}

} // namespace

void watchLanding(const Event& event, Vec3 link);

void onNpad(void* device) {
    const auto frame = g_reader.read(device);
    if (!frame.snapshot().freshSampleCount) return;
    const auto tick = now();
    {
        Hold hold(g_lock);
        if (g_event.active && g_event.previewTick && !g_event.leaveTick && tick - g_event.touched < kRecent) {
            g_previewStickX = static_cast<float>(frame.snapshot().rightStick.x) / 32767.0f;
            g_previewStickY = static_cast<float>(frame.snapshot().rightStick.y) / 32767.0f;
            g_previewStickTick = tick;
            frame.writeOwnedRightStick(0, 0);
        }
    }
    const auto aimed = g_aimTick.load(std::memory_order_relaxed);
    const bool aiming = aimed && tick - aimed < kRecent;
    const bool lapsed = !aimed || tick - aimed > kModeLapse;
    if (g_switch.update(aiming, lapsed, (frame.snapshot().buttons & kStickClick) != 0)) {
        g_descendMode.store(g_switch.descend, std::memory_order_release);
        __atomic_store_n(&g_promptDescend, g_switch.descend ? 1u : 0u, __ATOMIC_RELEASE);
        Logging.Log("[descend] mode %s", g_switch.descend ? "descend" : "ascend");
    }
    if (aiming) frame.maskOwnedButtons(kStickClick);
}

ClipperUpdate beforeClipperUpdate(void* clipper) {
    if (!g_descendMode.load(std::memory_order_acquire)) return {};
    const auto pending = reinterpret_cast<std::uintptr_t>(clipper) + off::clipper::kPending;
    const ClipperUpdate before{true, readMemory<std::uint32_t>(pending)};
    writeMemory<std::uint32_t>(pending, kSentinel);
    return before;
}

void afterClipperUpdate(void* clipperPointer, void* context, const ClipperUpdate& before) {
    using namespace off::clipper;
    const auto clipper = reinterpret_cast<std::uintptr_t>(clipperPointer);
    bool ran = true;
    if (before.armed) {
        ran = readMemory<std::uint32_t>(clipper + kPending) != kSentinel;
        if (!ran) writeMemory<std::uint32_t>(clipper + kPending, before.savedX);
    }
    // The callback supplies a live actor. Never sample a retained actor from the render thread.
    const auto actor = context ? readMemory<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(context) + 8) : 0;
    if (totk::engine::isPlausibleAddress(actor)) {
        const Vec3 live = readVec(actor + off::actor::kPosition);
        Event watched;
        {
            Hold hold(g_lock);
            if (g_event.active && g_event.actor != actor) {
                g_event.active = false;
                setOwned(0);
                forgetFall();
                Logging.Log("[descend] event cleared: live player changed");
            }
            if (g_event.actor == actor) { g_event.link = live; watched = g_event; }
        }
        watchLanding(watched, live);
    }
    if (readMemory<std::uint8_t>(clipper + kAiming) != 1) {
        __atomic_store_n(&g_markerTick, 0, __ATOMIC_RELAXED);
        g_proximity = {};
        return;
    }
    const auto tick = now();
    g_aimTick.store(tick, std::memory_order_relaxed);
    bool cleared = false;
    {
        Hold hold(g_lock);
        if (g_event.active) {
            g_event.active = false;
            cleared = true;
            setOwned(0);
        }
        if (!before.armed) g_armed = {};
    }
    if (cleared) {
        forgetFall();
        Logging.Log("[descend] event cleared: aiming resumed");
    }
    if (!before.armed) {
        __atomic_store_n(&g_markerTick, 0, __ATOMIC_RELAXED);
        g_proximity = {};
        g_verdictLogged = false;
        return;
    }
    if (!ran) return;

    if (!totk::engine::isPlausibleAddress(actor)) return;
    const Vec3 feet = readVec(actor + off::actor::kPosition);
    auto plan = engine::makePlan(actor, feet);
    const bool valid = plan.valid();

    const auto ms = tick / (engine::kTicksPerSecond / 1000);
    g_proximity.begin(actor, feet, ms);
    if (valid) {
        g_proximity.observe(feet, feet, true, true, ms);
    } else if (g_proximity.due(ms)) {
        bool refresh;
        auto point = g_proximity.next(feet, ms, refresh);
        const auto nearby = engine::makePlan(actor, point, 192);
        point.y = nearby.floor;
        // A hint across a wall is unhelpful, even if its exit is valid.
        const bool clear = nearby.valid() && std::isfinite(point.y) &&
            !engine::raycast(actor, pure::at(feet, feet.y + 0.8f), pure::at(point, point.y + 0.8f),
                             pure::Faces::All, pure::Layer::Solid).hit;
        const bool had = g_proximity.known;
        g_proximity.observe(feet, point, clear, refresh, ms);
        if (had != g_proximity.known)
            Logging.Log("[descend] nearby hint %s", g_proximity.known ? "found" : "expired");
    }
    const float warmth = valid ? 0 : g_proximity.warmth(feet);
    if (valid && __atomic_load_n(&g_warm, __ATOMIC_RELAXED)) {
        // Retire the silent hint copies; native success emission must still play its ready sound.
        const auto fade = reinterpret_cast<void (*)(std::uintptr_t, std::uint32_t)>(engine::mainBase + off::kFadeHandles);
        fade(clipper + 60, ~0u);
        fade(clipper + 92, ~0u);
    }
    __atomic_store_n(&g_warm, bits(warmth), __ATOMIC_RELAXED);
    __atomic_store_n(&g_cold, bits(1.0f - warmth), __ATOMIC_RELAXED);

    writeVec(clipper + kPending, valid ? plan.target : feet);
    const auto flags = readMemory<std::uint8_t>(clipper + kFlags);
    writeMemory<std::uint8_t>(clipper + kFlags,
                              static_cast<std::uint8_t>((flags & ~(kUndergroundFlags | kValid)) | (valid ? kValid : 0)));
    writeMemory<std::uint16_t>(clipper + kReasons, valid ? 0 : kReasonInvalid);
    publishMarker(feet, tick);
    Vec3 direction;
    float pitch;
    {
        Hold hold(g_lock);
        direction = g_aimDirection;
        pitch = g_aimPitch;
    }
    // The aim view camera stays in front of anything standing behind Link.
    const Vec3 pivot = pure::aimPivot(feet), spot = pure::aimSpot(feet, direction, pitch);
    const float reach = pure::aimReach(pivot, spot, engine::raycast(actor, pivot, spot, pure::Faces::All));
    {
        Hold hold(g_lock);
        g_armed = valid ? Armed{true,           feet,           plan.target,    plan.underside,
                                plan.drop,      tick,           plan.floor,     plan.openViews,
                                plan.rockVotes, plan.voxel,     plan.landing,   plan.landingFace}
                        : Armed{};
        g_aim = {actor, reach, tick, feet};
    }
    logVerdict(plan, feet);
}

DESCEND_INTEGER_ONLY bool warmMarker() {
    const auto stamp = __atomic_load_n(&g_markerTick, __ATOMIC_ACQUIRE);
    return stamp && svcGetSystemTick() - stamp <= kRecent && __atomic_load_n(&g_warm, __ATOMIC_RELAXED);
}

// Same live handle resolution/generation check as Handle::isValid and setAlphaScale.
DESCEND_INTEGER_ONLY std::uintptr_t markerEvent(std::uintptr_t handle) {
    const auto index = readMemory<std::int16_t>(handle + 2);
    const auto bank = readMemory<std::int8_t>(handle);
    if (index < 0 || bank < 0) return 0;
    const auto pools = readMemory<std::uintptr_t>(engine::mainBase + off::kEffectPools);
    const auto sizes = readMemory<std::uintptr_t>(engine::mainBase + off::kEffectSizes);
    const auto event = readMemory<std::uintptr_t>(pools + bank * 8) +
                       readMemory<std::uintptr_t>(sizes + bank * 8) * index;
    return event && readMemory<std::uint32_t>(event + 32) == readMemory<std::uint32_t>(handle + 4) ? event : 0;
}

bool blendMarker(std::uintptr_t clipper, unsigned greenOffset, unsigned effect) {
    if (!warmMarker()) return false;
    // A previous real green marker may still have its ready loop. Hints stay silent.
    const auto sound = markerEvent(clipper + greenOffset + 8);
    if (sound && !(readMemory<std::uint8_t>(sound + 8) & 0x10))
        reinterpret_cast<void (*)(std::uintptr_t)>(engine::mainBase + off::kFadeEvent)(sound);
    for (unsigned i = 0; i < 2; ++i) {
        const auto handle = clipper + greenOffset + i * 16;
        const auto event = markerEvent(handle);
        if (!event || (readMemory<std::uint8_t>(event + 8) & 0x10)) {
            // Native return is an indirect HandleSet through X8, despite its 16-byte payload.
            struct IndirectHandles { std::uint64_t effect, sound, unused; };
            const auto emit = reinterpret_cast<IndirectHandles (*)(std::uintptr_t, unsigned)>(
                engine::mainBase + off::kEmitPowerEffect);
            const auto handles = emit(0, effect + i);
            writeMemory(handle, handles.effect);
            writeMemory(handle + 8, handles.sound);
        }
    }
    return true;
}

DESCEND_INTEGER_ONLY void onMarkerCast(std::uintptr_t query, std::uintptr_t clipper) {
    const auto stamp = __atomic_load_n(&g_markerTick, __ATOMIC_ACQUIRE);
    const bool fresh = stamp && svcGetSystemTick() - stamp <= kRecent;
    const bool warm = fresh && __atomic_load_n(&g_warm, __ATOMIC_RELAXED);
    if (warm || g_blendOwner == clipper) {
        for (unsigned i = 0; i < 4; ++i) {
            const auto event = markerEvent(clipper + 60 + i * 16);
            if (!event) continue;
            const auto alpha = warm ? __atomic_load_n(i & 1 ? &g_cold : &g_warm, __ATOMIC_RELAXED) : 0x3F800000u;
            writeMemory(event + 196, alpha);
            writeMemory(event + 420, readMemory<std::uint32_t>(event + 420) | 2u);
        }
        g_blendOwner = warm ? clipper : 0;
    }
    if (!fresh) return;
    volatile std::uint32_t* const start = reinterpret_cast<volatile std::uint32_t*>(query + off::marker::kStart);
    volatile std::uint32_t* const end = reinterpret_cast<volatile std::uint32_t*>(query + off::marker::kEnd);
    volatile std::uint32_t* const miss = reinterpret_cast<volatile std::uint32_t*>(query + off::marker::kMiss);
    for (int i = 0; i < 3; ++i) {
        start[i] = __atomic_load_n(&g_marker[i], __ATOMIC_RELAXED);
        end[i] = __atomic_load_n(&g_marker[3 + i], __ATOMIC_RELAXED);
        miss[i] = __atomic_load_n(&g_marker[6 + i], __ATOMIC_RELAXED);
    }
}

void beforeSelectorPosition(void* selectorPointer) {
    const auto selector = reinterpret_cast<std::uintptr_t>(selectorPointer);
    if (!std::isnan(readMemory<float>(selector + off::selector::kCeilingHeight))) return;
    if (!resolve(engine::nodeActor(selectorPointer), selector)) return;
    float startHeight;
    {
        Hold hold(g_lock);
        startHeight = g_event.start.y;
    }
    // A known height skips the selector's upward ceiling search for this event.
    writeMemory<float>(selector + off::selector::kCeilingHeight, startHeight);
}

bool ownsRiseStage(void* node) { return resolve(engine::nodeActor(node), 0); }

// Use the fall pose during descent. Preview, upward return, A-drop and ordinary Ascend keep their native animation sequences; Clay rejected the upward-swim substitution in patch02.
bool overrideTravelAnimation(void* node) {
    const auto base = reinterpret_cast<std::uintptr_t>(node);
    const auto actor = engine::nodeActor(node);
    const auto player = engine::playerComponent(actor);
    if (!player || !(readMemory<std::uint8_t>(base + off::node::kFlags) & off::node::kAnimationEnabled))
        return false;
    const auto state = readMemory<std::uint32_t>(base + off::main_stage::kState);
    {
        Hold hold(g_lock);
        if (!g_event.active || g_event.actor != actor || g_event.released) return false;
        if (state >= off::main_stage::kPreview ||
            !pure::sameBits(readVec(base + off::main_stage::kTarget), g_event.target)) {
            return false;
        }
        if (!g_event.sinkPoseLogged) {
            g_event.sinkPoseLogged = true;
            Logging.Log("[descend] event %u body pose: downward fall", g_event.serial);
        }
    }
    engine::ensureAnimation(player, off::kCommandFall);
    return true;
}

bool holdsTravelTarget(void* actor, const void* in) {
    Vec3 target;
    std::memcpy(&target, in, sizeof(target));
    Hold hold(g_lock);
    return g_event.active && g_event.actor == reinterpret_cast<std::uintptr_t>(actor) &&
           pure::sameBits(target, g_event.target);
}

TravelState beforeTravelState(void* node) {
    const auto actor = engine::nodeActor(node);
    const auto base = reinterpret_cast<std::uintptr_t>(node);
    const Vec3 target = readVec(base + off::main_stage::kTarget);
    std::uintptr_t player;
    {
        Hold hold(g_lock);
        if (!g_event.active || g_event.actor != actor || !pure::sameBits(target, g_event.target)) return {};
        player = g_event.player;
    }
    return {true, readMemory<std::uint32_t>(base + off::main_stage::kState),
            readMemory<float>(base + off::main_stage::kPreviewTimer),
            (readMemory<std::uint8_t>(player + off::player::kCancelByte) & off::player::kCancelBit) != 0};
}

void afterTravelState(void* node, const TravelState& before) {
    using namespace off::main_stage;
    if (!before.owned) return;
    const auto state = readMemory<std::uint32_t>(reinterpret_cast<std::uintptr_t>(node) + kState);
    const auto tick = now();
    // The game leaves the preview with time remaining only through the confirm press; the drop rides the cancel stage, which carries Link to its return target with collision off until the last metre.
    const bool leaving = before.state == kPreview && state == kLeaving;
    const bool confirmed = leaving && !before.cancelled && before.timer > kPreviewTimerRunning;
    Event event;
    bool firstPreview = false;
    std::uint64_t gap;
    {
        Hold hold(g_lock);
        if (!g_event.active) return;
        gap = tick - g_event.touched;
        g_event.touched = tick;
        g_event.link = readVec(engine::nodeActor(node) + off::actor::kPosition);
        setOwned(g_event.serial);
        if (state == kPreview && !g_event.previewTick) {
            g_event.previewTick = tick;
            firstPreview = true;
        }
        if (leaving && !g_event.leaveTick) {
            g_event.leaveTick = tick;
            g_event.dropped = confirmed;
        }
        event = g_event;
    }
    if (gap > kPauseGap)
        Logging.Log("[descend] event %u resumed after %u ms without updates (state %u)", event.serial,
                    static_cast<unsigned>(gap / ticks(1)), state);
    if (firstPreview) {
        Logging.Log("[descend] event %u preview reached", event.serial);
    }
    if (state == kPreview && !event.leaveTick) {
        pure::Pose wanted;
        {
            Hold hold(g_lock);
            const float seconds = g_event.orbitTick
                ? static_cast<float>(tick - g_event.orbitTick) / static_cast<float>(engine::kTicksPerSecond) : 0;
            const bool fresh = g_previewStickTick && tick - g_previewStickTick < kRecent;
            g_event.orbit.move(fresh ? g_previewStickX : 0, fresh ? g_previewStickY : 0, seconds);
            g_event.orbitTick = tick;
            wanted = g_event.orbit.pose(event.target, event.underside);
        }
        const auto safe = pure::clearPreviewPose(wanted,
            [actor = engine::nodeActor(node)](Vec3 from, Vec3 to, pure::Faces faces, pure::Layer layer) {
                return engine::raycast(actor, from, to, faces, layer);
            });
        if (safe) {
            Hold hold(g_lock);
            if (g_event.serial == event.serial) {
                g_event.previewPose = *safe;
                g_event.previewReady = true;
            }
        } else {
            // Geometry changed or even the centre cannot fit a camera. Use the existing return path.
            {
                Hold hold(g_lock);
                if (g_event.serial == event.serial) g_event.previewReady = false;
            }
            requestCancel(event.player, event.selector);
            setSwirl(event.serial, true, "preview camera blocked; returning");
        }
    }
    if (leaving && !confirmed)
        setSwirl(event.serial, true, before.cancelled ? "returning after B" : "returning after the timeout");
    if (leaving) {
        if (before.cancelled) {
            Logging.Log("[descend] event %u preview cancelled: returning to start", event.serial);
        } else {
            if (confirmed) {
                writeVec(event.player + off::player::kReturnTarget, event.drop);
                __atomic_store_n(&g_fallPlayer, event.player, __ATOMIC_RELAXED);
                __atomic_store_n(&g_fallTick, tick, __ATOMIC_RELEASE);
            }
            requestCancel(event.player, event.selector);
            Logging.Log("[descend] event %u %s", event.serial,
                        confirmed ? "confirmed: dropping below" : "preview timed out: returning to start");
        }
    }
}

DESCEND_INTEGER_ONLY std::uintptr_t cancelCommand(std::uintptr_t player, std::uintptr_t command) {
    const auto stamp = __atomic_load_n(&g_fallTick, __ATOMIC_ACQUIRE);
    if (!stamp || svcGetSystemTick() - stamp > kArmedLife) return command;
    if (player != __atomic_load_n(&g_fallPlayer, __ATOMIC_RELAXED) || command != engine::mainBase + off::kCommandSwirl)
        return command;
    __atomic_store_n(&g_fallTick, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&g_swirlSkipped, 1, __ATOMIC_RELEASE);
    return engine::mainBase + off::kCommandFall;
}

// The A prompt's "Ascend" is message 0028 of LayoutMsg/DoCommand_00, and no game message says "Descend". While Descend aim is on, the found message becomes the raw text "Descend" with the raw-string id the game's own eui::MessageString::setMessage leaves; the prompt screen redraws once each time that id changes.
DESCEND_INTEGER_ONLY void onPromptMessage(std::uintptr_t message, std::uintptr_t label, bool setText) {
    using namespace off::prompt;
    if (!__atomic_load_n(&g_promptDescend, __ATOMIC_ACQUIRE) || !label) return;
    const auto* const name = reinterpret_cast<const volatile char*>(label);
    if (name[0] != '0' || name[1] != '0' || name[2] != '2' || name[3] != '8' || name[4]) return;
    if (setText) {
        *reinterpret_cast<volatile std::uintptr_t*>(message + kText) = reinterpret_cast<std::uintptr_t>(kDescendText);
        *reinterpret_cast<volatile std::int32_t*>(message + kLength) = sizeof(kDescendText) / sizeof(char16_t) - 1;
    }
    *reinterpret_cast<volatile std::uint64_t*>(message + kId) = kRawStringId;
}

// This is a graphics-scene flag, not just an emitter switch. Native scene update, background and particle draws all consume it. Keep it through rendering; clearing it after effect calculation left the swirl with the ordinary world's render setup. Synchronize before both effect calculation and scene preparation, never around draw calls.
DESCEND_INTEGER_ONLY void synchronizeScreenCover() {
    using namespace off::ceiling_effect;
    const auto stamp = __atomic_load_n(&g_effectTick, __ATOMIC_ACQUIRE);
    const bool owned = __atomic_load_n(&g_ownedSerial, __ATOMIC_ACQUIRE) && stamp &&
                       svcGetSystemTick() - stamp <= ticks(pure::tuning::kStaleMs);
    if (!owned && !__atomic_load_n(&g_forcedCoverOwner, __ATOMIC_ACQUIRE)) return;
    const auto slot = *reinterpret_cast<const volatile std::uintptr_t*>(engine::mainBase + kGlobalSlot);
    const auto global = slot ? *reinterpret_cast<const volatile std::uintptr_t*>(slot) : 0;
    const auto owner = global ? *reinterpret_cast<const volatile std::uintptr_t*>(global + kOwner) : 0;
    if (!owner) {
        __atomic_store_n(&g_forcedCoverOwner, 0, __ATOMIC_RELEASE);
        return;
    }
    auto* const flags = reinterpret_cast<volatile std::uint32_t*>(owner + kFlags);
    const std::uint32_t value = *flags;
    if (!owned) {
        if (__atomic_exchange_n(&g_forcedCoverOwner, 0, __ATOMIC_ACQ_REL) == owner)
            *flags = value & ~kInCeiling;
        return;
    }
    if (__atomic_load_n(&g_swirlWanted, __ATOMIC_ACQUIRE)) {
        __atomic_add_fetch(&g_effectShown, 1, __ATOMIC_RELEASE);
        if (value & kInCeiling) {
            __atomic_add_fetch(&g_effectNative, 1, __ATOMIC_RELEASE);
            return;
        }
        *flags = value | kInCeiling;
        __atomic_store_n(&g_forcedCoverOwner, owner, __ATOMIC_RELEASE);
        return;
    }
    __atomic_store_n(&g_forcedCoverOwner, 0, __ATOMIC_RELEASE);
    if (value & kInCeiling) {
        *flags = value & ~kInCeiling;
        __atomic_add_fetch(&g_effectHeld, 1, __ATOMIC_RELEASE);
    }
}

DESCEND_INTEGER_ONLY std::uint64_t effectDirection(std::uint64_t native) {
    const auto stamp = __atomic_load_n(&g_effectTick, __ATOMIC_ACQUIRE);
    if (!stamp || svcGetSystemTick() - stamp > ticks(pure::tuning::kStaleMs)) return native;
    if (!__atomic_load_n(&g_ownedSerial, __ATOMIC_ACQUIRE)) return native;
    // Main inherits Ascend's Up variant. Select Down while descending and Up while returning.
    return __atomic_load_n(&g_returnEffect, __ATOMIC_ACQUIRE) ? 1 : 0;
}

// After A the stage skips its wait for the swirl and moves Link down at a falling speed with no slowdown near the goal.
void beforeCancelStage(void* node) {
    using namespace off::cancel_stage;
    const auto actor = engine::nodeActor(node);
    const auto base = reinterpret_cast<std::uintptr_t>(node);
    const auto tick = now();
    Event event;
    bool first = false, fallStarted = false;
    {
        Hold hold(g_lock);
        if (!g_event.active || g_event.actor != actor) return;
        // B during the sink leaves the travel stage without a preview exit.
        if (!g_event.leaveTick) g_event.leaveTick = tick;
        first = !g_event.cancelTick;
        g_event.cancelTick = g_event.touched = tick;
        g_event.link = readVec(actor + off::actor::kPosition);
        setOwned(g_event.serial);
        __atomic_store_n(&g_returnEffect, g_event.dropped ? 0u : 1u, __ATOMIC_RELEASE);
        if (g_event.dropped && !g_event.fallTick) {
            g_event.fallTick = tick;
            fallStarted = true;
        }
        event = g_event;
    }
    if (first) {
        const Vec3 link = readVec(actor + off::actor::kPosition);
        const Vec3 goal = readVec(base + kGoal);
        Logging.Log("[descend] event %u cancel stage started (%s) link_y_mm=%d goal_y_mm=%d", event.serial,
                    event.dropped ? "drop" : "return", mm(link.y), mm(goal.y));
        // B during the sink returns without a preview, with the effect still on.
        if (!event.dropped) setSwirl(event.serial, true, "returning");
    }
    if (!event.dropped || event.released) return;
    if (fallStarted) {
        forgetFall();
        const bool skipped = __atomic_exchange_n(&g_swirlSkipped, 0, __ATOMIC_ACQ_REL) != 0;
        const auto state = readMemory<std::uint32_t>(base + kState);
        if (state == kSwirl) {
            // The game's own step when the swirl finishes, except that it enters a falling state directly.
            writeMemory<std::uint32_t>(base + kState, kFalling);
            writeMemory<std::uint64_t>(base + kClothWind, readMemory<std::uint64_t>(actor + off::actor::kClothWind));
            writeMemory<std::uint32_t>(base + kClothWind + 8,
                                       readMemory<std::uint32_t>(actor + off::actor::kClothWind + 8));
        }
        writeMemory<float>(base + kSlowdown, 0);
        Logging.Log("[descend] event %u fall started: swirl %s, stage state %u", event.serial,
                    skipped ? "replaced by the fall" : "not replaced", state);
    }
    const float seconds = static_cast<float>(tick - event.fallTick) / static_cast<float>(engine::kTicksPerSecond);
    writeMemory<float>(base + kSpeed, pure::dropStep(seconds));
}

void markReleased(std::uint32_t serial) {
    Hold hold(g_lock);
    if (g_event.serial == serial) g_event.released = true;
}

void afterCancelLeave(void* node) {
    const auto actor = engine::nodeActor(node);
    const auto tick = now();
    Hold hold(g_lock);
    if (!g_event.active || g_event.actor != actor) return;
    g_event.link = readVec(actor + off::actor::kPosition);
    g_event.released = true;
    g_event.cancelTick = g_event.touched = tick;
    __atomic_store_n(&g_returnEffect, 0, __ATOMIC_RELEASE);
    setSwirl(g_event.serial, false, "native cancel stage left");
    forgetFall();
    Logging.Log("[descend] event %u native cancel stage left; camera handing back", g_event.serial);
}

// After B or a timeout: once collision is back and Link's feet are above his start, the effect goes off and the stage ends, so he stands on the floor.
void landReturn(std::uintptr_t base, std::uintptr_t actor, const Event& event, bool collision) {
    using namespace off::cancel_stage;
    const Vec3 link = readVec(actor + off::actor::kPosition);
    const auto ms = static_cast<unsigned>((now() - event.leaveTick) / ticks(1));
    const auto state = readMemory<std::uint32_t>(base + kState);
    if (!pure::returnReleases(collision, link.y, event.start.y)) {
        if (state == kArrived && !event.goalLogged) {
            {
                Hold hold(g_lock);
                if (g_event.serial == event.serial) g_event.goalLogged = true;
            }
            Logging.Log("[descend] event %u return reached the goal without release at %u ms: collision=%d "
                        "link_y_mm=%d start_y_mm=%d",
                        event.serial, ms, collision ? 1 : 0, mm(link.y), mm(event.start.y));
        }
        return;
    }
    writeMemory<std::uint8_t>(base + kDone, 1);
    markReleased(event.serial);
    setSwirl(event.serial, false, "collision back");
    Logging.Log("[descend] event %u returned at %u ms: link_y_mm=%d above_start_mm=%d step_mm=%d state %u",
                event.serial, ms, mm(link.y), mm(link.y - event.start.y), mm(readMemory<float>(base + kSpeed)), state);
}

// Once the stage has given collision back below the underside, it ends and Link carries on in a normal fall.
void afterCancelStage(void* node) {
    using namespace off::cancel_stage;
    const auto actor = engine::nodeActor(node);
    const auto base = reinterpret_cast<std::uintptr_t>(node);
    Event event;
    {
        Hold hold(g_lock);
        if (!g_event.active || g_event.actor != actor || g_event.released) return;
        event = g_event;
    }
    const bool collision =
        !(readMemory<std::uint32_t>(event.player + off::player::kEventFlags) & off::player::kCollisionOff);
    if (!event.dropped) {
        landReturn(base, actor, event, collision);
        return;
    }
    const Vec3 link = readVec(actor + off::actor::kPosition);
    const auto state = readMemory<std::uint32_t>(base + kState);
    const auto ms = static_cast<unsigned>((now() - event.fallTick) / ticks(1));
    if (!collision || !(link.y <= event.underside - pure::tuning::kReleaseDepth)) {
        if (state == kArrived && !event.goalLogged) {
            {
                Hold hold(g_lock);
                if (g_event.serial == event.serial) g_event.goalLogged = true;
            }
            Logging.Log("[descend] event %u fall reached the goal without release at %u ms: collision=%d "
                        "link_y_mm=%d",
                        event.serial, ms, collision ? 1 : 0, mm(link.y));
        }
        return;
    }
    writeMemory<std::uint8_t>(base + kDone, 1);
    {
        Hold hold(g_lock);
        if (g_event.serial == event.serial) {
            g_event.released = true;
            g_event.releaseTick = g_event.lowestTick = now();
            g_event.releaseY = g_event.lowestY = link.y;
        }
    }
    Logging.Log("[descend] event %u released into a fall at %u ms: link_y_mm=%d below_underside_mm=%d step_mm=%d "
                "state %u",
                event.serial, ms, mm(link.y), mm(event.underside - link.y), mm(readMemory<float>(base + kSpeed)),
                state);
}

constexpr float kLandStill = 0.05f;
constexpr std::uint64_t kLandSettled = ticks(400), kFallGiveUp = ticks(8000);

// After the drop's release: where Link stops going down, or where he is after kFallGiveUp, logged once with the game's voxel look from just above him. Called only from the live player's clipper callback.
void watchLanding(const Event& event, Vec3 link) {
    if (!event.dropped || !event.releaseTick || event.landLogged) return;
    Event watched;
    bool stopped;
    std::uint64_t tick;
    {
        Hold hold(g_lock);
        if (g_event.serial != event.serial || g_event.landLogged) return;
        tick = now();
        if (link.y < g_event.lowestY - kLandStill) {
            g_event.lowestY = link.y;
            g_event.lowestTick = tick;
        }
        stopped = tick - g_event.lowestTick >= kLandSettled;
        if (!stopped && tick - g_event.releaseTick < kFallGiveUp) return;
        g_event.landLogged = true;
        watched = g_event;
    }
    char voxelText[128];
    formatVoxel(voxelText, sizeof(voxelText), engine::voxelUp(pure::at(link, link.y + pure::tuning::kLandingProbe)));
    const auto ms = static_cast<unsigned>(((stopped ? watched.lowestTick : tick) - watched.releaseTick) / ticks(1));
    Logging.Log("[descend] event %u %s %u ms after release: link_mm=(%d,%d,%d) lowest_y_mm=%d below_release_mm=%d "
                "below_underside_mm=%d planned_land_mm=%d above_planned_mm=%d %s",
                watched.serial, stopped ? "stopped going down" : "still falling", ms, mm(link.x), mm(link.y),
                mm(link.z), mm(watched.lowestY), mm(watched.releaseY - link.y), mm(watched.underside - link.y),
                mm(watched.landing), mm(link.y - watched.landing), voxelText);
}

// Every camera node has run by now; the override set here becomes this frame's render camera. The up direction has no override, so the game's own is swapped for a level one and put back after the update.
CameraUp beforeCameraUpdate(void* componentPointer) {
    const auto component = reinterpret_cast<std::uintptr_t>(componentPointer);
    Event event;
    Aim aim;
    {
        Hold hold(g_lock);
        event = g_event;
        aim = g_aim;
    }
    if (!event.active) return aimCamera(component, aim);
    const auto tick = now();
    const auto phase = pure::phaseAt(timeline(event), milliseconds(tick));
    if (phase == pure::Phase::Off) {
        Hold hold(g_lock);
        if (g_event.serial == event.serial && g_event.touched == event.touched &&
            pure::phaseAt(timeline(g_event), milliseconds(tick)) == pure::Phase::Off) {
            setOwned(0);
            if (event.released) {
                g_event.active = false;
                forgetFall();
                Logging.Log("[descend] event %u completed; camera ownership released", event.serial);
            }
        }
        return {};
    }
    // Suppress the old camera's exit swirl through handback, then phase Off retires this ownership.
    {
        Hold hold(g_lock);
        if (!g_event.active || g_event.serial != event.serial) return {};
        setOwned(event.serial);
    }
    const Vec3 link = event.link;
    const auto native = nativePose(component);
    pure::Pose pose;
    bool uncover = false;
    {
        Hold hold(g_cameraLock);
        g_aimBlend = {}; // the next aim view blends in from the game's own view
        const auto held = __atomic_load_n(&g_effectHeld, __ATOMIC_ACQUIRE);
        if (held != g_effectLogged) {
            g_effectLogged = held;
            Logging.Log("[descend] event %u in-ceiling screen effect held off at %u ms (%u so far): link_y_mm=%d",
                        event.serial, static_cast<unsigned>((tick - event.startTick) / ticks(1)), held, mm(link.y));
        }
        const int index = cameraIndex(event, component, link, native, tick);
        if (index < 0 || !framePose(event, index, tick, link, native, pose, uncover) || !finite(pose)) return {};
        ++g_camera.cameras[index].writes;
    }
    auto saved = showPose(component, pose);
    if (saved.saved && uncover) {
        saved.uncoverSerial = event.serial;
        saved.previewTick = event.previewTick;
    }
    return saved;
}

void afterCameraUpdate(void* component, const CameraUp& saved) {
    if (saved.saved) writeVec(reinterpret_cast<std::uintptr_t>(component) + off::game_camera::kUp, saved.up);
    if (!saved.saved || !saved.uncoverSerial) return;
    // Only uncover a successfully applied, current preview. A late camera snapshot must never turn off the cover for a newer descent or a return that began while the native camera was updating.
    Hold hold(g_lock);
    if (g_event.active && g_event.serial == saved.uncoverSerial && g_event.previewReady &&
        g_event.previewTick == saved.previewTick && !g_event.leaveTick && !g_event.cancelTick && !g_event.released)
        setSwirl(g_event.serial, false, "preview camera applied");
}

// The game fades Link out as its own camera nears him (the player camera's PlayerAlpha0Dist/PlayerAlpha1Dist), and Ascend's aim camera sits about a metre from him. While the aim view is shown the fade is measured from that view instead.
CameraFade beforeCameraAlpha(void* componentPointer) {
    using namespace off::game_camera;
    const auto component = reinterpret_cast<std::uintptr_t>(componentPointer);
    const auto tick = now();
    pure::Pose pose;
    bool log;
    {
        Hold hold(g_cameraLock);
        const auto* const shown = recentAimPose(component, tick);
        if (!shown) return {};
        pose = shown->pose;
        log = !g_fadeLogged;
        g_fadeLogged = true;
    }
    const CameraFade saved{true, readVec(component + kFadePosition), readVec(component + kFadeLookAt)};
    writeVec(component + kFadePosition, pose.position);
    writeVec(component + kFadeLookAt, pose.lookAt);
    if (log)
        Logging.Log("[descend] aim view fade measured from the view: game_camera_mm=(%d,%d,%d) view_mm=(%d,%d,%d)",
                    mm(saved.position.x), mm(saved.position.y), mm(saved.position.z), mm(pose.position.x),
                    mm(pose.position.y), mm(pose.position.z));
    return saved;
}

void afterCameraAlpha(void* componentPointer, const CameraFade& saved) {
    if (!saved.saved) return;
    const auto component = reinterpret_cast<std::uintptr_t>(componentPointer);
    writeVec(component + off::game_camera::kFadePosition, saved.position);
    writeVec(component + off::game_camera::kFadeLookAt, saved.lookAt);
}

// While Ascend aims no event camera runs, so the fade that hides Link is the player camera's own step, measured from the camera floats its update just wrote: Ascend's camera about a metre from him. While the aim view is shown those floats hold the view for the fade and are put back before the update copies them on.
CameraFade beforePlayerFade(void* cameraPointer) {
    using namespace off::player_camera;
    const auto camera = reinterpret_cast<std::uintptr_t>(cameraPointer);
    const auto tick = now();
    pure::Pose pose;
    bool log;
    {
        Hold hold(g_cameraLock);
        const auto* const shown = newestAimPose(tick);
        if (!shown) return {};
        pose = shown->pose;
        log = !g_playerFadeLogged;
        g_playerFadeLogged = true;
    }
    const CameraFade saved{true, readVec(camera + kFadePosition), readVec(camera + kFadeLookAt)};
    if (!finite({saved.position, saved.lookAt}) || !pure::cameraInRange(saved.position, pose.position)) {
        if (log)
            Logging.Log("[descend] aim view player fade left alone: camera_mm=(%d,%d,%d) view_mm=(%d,%d,%d)",
                        mm(saved.position.x), mm(saved.position.y), mm(saved.position.z), mm(pose.position.x),
                        mm(pose.position.y), mm(pose.position.z));
        return {};
    }
    writeVec(camera + kFadePosition, pose.position);
    writeVec(camera + kFadeLookAt, pose.lookAt);
    if (log)
        Logging.Log("[descend] aim view player fade measured from the view: camera_mm=(%d,%d,%d) look_mm=(%d,%d,%d) "
                    "view_mm=(%d,%d,%d)",
                    mm(saved.position.x), mm(saved.position.y), mm(saved.position.z), mm(saved.lookAt.x),
                    mm(saved.lookAt.y), mm(saved.lookAt.z), mm(pose.position.x), mm(pose.position.y),
                    mm(pose.position.z));
    return saved;
}

void afterPlayerFade(void* cameraPointer, const CameraFade& saved) {
    if (!saved.saved) return;
    const auto camera = reinterpret_cast<std::uintptr_t>(cameraPointer);
    writeVec(camera + off::player_camera::kFadePosition, saved.position);
    writeVec(camera + off::player_camera::kFadeLookAt, saved.lookAt);
}

} // namespace descend::feature
