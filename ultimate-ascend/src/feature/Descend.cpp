// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "feature/Descend.hpp"

#include <lib.hpp>

#include "engine/Game.hpp"
#include "program/loggers.hpp"
#include "pure/DescendGeometry.hpp"
#include "pure/Proximity.hpp"
#include "pure/MarkerScale.hpp"
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
                        kEventIdle = ticks(3000), kPauseGap = ticks(1000);
constexpr int kMaxCameras = 4;

std::atomic<bool> g_inShrine{false};
std::atomic<std::uintptr_t> g_scaleOwner{0};
float g_scaleOriginY = 0;

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
    pure::Plan::Route route{};
    pure::BodyKey body{};
    Vec3 localOrigin{};
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
    std::uint64_t exitWaitTick = 0;
    float landing = NAN;
    Vec3 link{}; // published only by hooks receiving the live actor/node; render hooks never dereference actor
    float focusY = 0, focusOffset = 0;
    bool focusReady = false;
    pure::PreviewOrbit orbit{};
    pure::Pose previewPose{};
    bool previewReady = false;
    std::uint64_t orbitTick = 0;
    pure::Plan::Route route{};
    pure::BodyKey body{};
    Vec3 committed{};
    Vec3 localOrigin{};
    bool exitValidated = false, exitRechecked = false, routeFailed = false;
};

struct Camera {
    std::uintptr_t component = 0;
    pure::Phase phase = pure::Phase::Off;
    std::uint64_t phaseTick = 0;
    bool written = false;
    pure::Pose from{}, last{};
};

struct Aim {
    std::uintptr_t actor = 0;
    float reach = 1;
    std::uint64_t tick = 0;
    Vec3 link{};
};

struct AimShown {
    std::uintptr_t component = 0;
    pure::Pose pose{};
    std::uint64_t tick = 0;
};

struct CameraState {
    std::uint32_t serial = 0;
    int view = -1;
    Vec3 behind{};
    Camera cameras[kMaxCameras]{};
    int cameraCount = 0;
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
std::atomic<std::uint64_t> g_aimTick{0};
// Read by the marker inline hook, which cannot inline std::atomic; accessed through __atomic builtins.
std::uint64_t g_markerTick = 0;
std::atomic<bool> g_descendMode{false};
std::uint32_t g_marker[9]{}; // cast start, cast end, miss position
pure::Proximity g_proximity;
std::uint32_t g_warm = 0, g_cold = 0x3F800000;
std::uintptr_t g_blendOwner = 0; // compared only with the live postCalc argument
std::uintptr_t g_fallPlayer = 0;
std::uint64_t g_fallTick = 0;
std::uint32_t g_ownedSerial = 0;
// Screen-cover request shared with the effect hook through __atomic builtins.
std::uint32_t g_swirlWanted = 0;
std::uint32_t g_returnEffect = 0;
std::uint64_t g_effectTick = 0;
// Compared with the current graphics owner before cleanup; never dereferenced after a scene change.
std::uintptr_t g_forcedCoverOwner = 0;
std::uint32_t g_promptDescend = 0;
constexpr char16_t kDescendText[] = u"Descend";
constexpr char16_t kDepthsText[] = u"Depths";
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

void setSwirl(std::uint32_t serial, bool on, const char* why) {
    const std::uint32_t wanted = on ? 1 : 0;
    if (__atomic_exchange_n(&g_swirlWanted, wanted, __ATOMIC_ACQ_REL) == wanted) return;
    Logging.Log("[descend] event %u cover %s: %s", serial, on ? "on" : "off", why);
}

int mm(float value) { return std::isfinite(value) ? static_cast<int>(value * 1000) : -999999; }


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

// Keep refusal reasons and boundary evidence within one bounded log record.
void logVerdict(const pure::Plan& plan, Vec3 feet) {
    if (g_verdictLogged && plan.verdict == g_loggedVerdict) return;
    g_verdictLogged = true;
    g_loggedVerdict = plan.verdict;
    Logging.Log("[descend] target %s route=%u floor_mm=%d underside_mm=%d rock=%u body=%d voxel=%u/%d/%d",
                pure::verdictName(plan.verdict), unsigned(plan.route), mm(plan.floor), mm(plan.underside),
                unsigned(plan.rockReason), plan.bodyLine, unsigned(plan.voxel.answer),
                mm(plan.voxel.firstSolid), mm(plan.voxel.solidEnd));
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

bool resolve(std::uintptr_t actor, std::uintptr_t selector) {
    const auto clipper = engine::ceilingClipper(actor);
    const auto player = engine::playerComponent(actor);
    if (!clipper || !player) return false;
    const Vec3 committed = readVec(clipper + off::clipper::kCommitted);
    const auto tick = now();
    Event started{};
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
            g_event.route = g_armed.route;
            g_event.body = g_armed.body;
            g_event.committed = committed;
            g_event.localOrigin = g_armed.localOrigin;
            g_event.exitValidated = g_armed.route != pure::Plan::Route::DeepTerrain;
            g_event.link = readVec(actor + off::actor::kPosition);
            const Vec3 behind{-g_aimDirection.x, 0, -g_aimDirection.z};
            g_event.previewPose = pure::previewPose(g_armed.target, g_armed.underside,
                                                    pure::chooseView(g_armed.openViews, behind), behind);
            g_event.orbit.begin(g_event.previewPose);
            g_armed = {};
            writeVec(player + off::player::kReturnTarget,
                     {start.x, start.y + pure::tuning::kReturnLift, start.z});
            started = g_event;
            __atomic_store_n(&g_swirlWanted, 1, __ATOMIC_RELEASE);
            setOwned(g_event.serial);
        } else if (g_event.active && g_event.actor == actor && pure::sameBits(committed, g_event.committed) &&
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
    Logging.Log("[descend] event %u route=%u start_mm=(%d,%d,%d) underside_mm=%d land_mm=%d",
                started.serial, unsigned(started.route), mm(started.start.x), mm(started.start.y), mm(started.start.z),
                mm(started.underside), mm(started.landing));
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
            milliseconds(event.cancelTick), milliseconds(event.touched), event.dropped, event.released};
}

std::uint64_t previewPoseTick(const Event& event) {
    return event.previewTick + (event.route == pure::Plan::Route::DeepTerrain
        ? ticks(pure::tuning::kDeepPreviewDelayMs) : 0);
}

// This frame's pose for one camera of the event, or false once it is handed back; g_cameraLock held.
bool framePose(const Event& event, int index, std::uint64_t tick, Vec3 link, const pure::Pose& native,
               pure::Pose& pose, bool& uncover) {
    auto& camera = g_camera.cameras[index];
    auto phase = pure::phaseAt(timeline(event), milliseconds(tick));
    const auto previewStart = previewPoseTick(event);
    if (phase == pure::Phase::Preview && tick < previewStart) phase = pure::Phase::Sink;
    if (phase != camera.phase) {
        camera.from = camera.written ? camera.last : native;
        camera.phase = phase;
        camera.phaseTick = tick;
    }
    if (phase == pure::Phase::Off) return false;
    const pure::CameraScene scene{link, event.target, event.underside, g_camera.view, g_camera.behind, native};
    // All preview cameras share the same clock, so one cannot uncover another camera's unfinished blend.
    const auto blendStart = phase == pure::Phase::Preview ? previewStart : camera.phaseTick;
    const float weight = static_cast<float>(tick - blendStart) / static_cast<float>(ticks(pure::blendMs(phase)));
    auto destination = pure::phasePose(phase, scene);
    if (event.previewReady && phase == pure::Phase::Preview) destination = event.previewPose;
    if (event.previewReady && phase == pure::Phase::Drop)
        destination = {event.previewPose.position,
                       {link.x, std::fmin(event.previewPose.lookAt.y, link.y + pure::tuning::kTrackLook), link.z}};
    pose = pure::blend(camera.from, destination, weight);
    // The actor origin stays at the ceiling while the preview animation moves the body below it.
    if (event.route == pure::Plan::Route::DeepTerrain && event.focusReady &&
        (phase == pure::Phase::Sink || phase == pure::Phase::Preview)) pose.lookAt.y = event.focusY;
    uncover = phase == pure::Phase::Preview && event.previewReady && weight >= 1.0f;
    camera.last = pose;
    camera.written = true;
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
    if (c.cameraCount == kMaxCameras || !pure::cameraInRange(native.position, link)) return -1;
    if (!c.cameraCount) {
        const Vec3 look = pure::horizontalDirection(native);
        c.behind = {-look.x, 0, -look.z};
        c.view = pure::chooseView(event.openViews, c.behind);
    }
    const int index = c.cameraCount++;
    auto& camera = c.cameras[index] = Camera{component};
    for (const auto& shown : g_aimShown) {
        if (shown.component != component || !shown.tick || tick - shown.tick > kRecent) continue;
        camera.last = shown.pose;
        camera.written = true;
    }
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
        }
        pitch = pure::aimPitch(nativePitch, g_aimPitchStart);
        pose = pure::blend(native, pure::aimPose(link, direction, pitch, aim.reach), g_aimBlend.weight(ms));
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
    const bool restricted = g_inShrine.load(std::memory_order_relaxed);
    const bool aiming = !restricted && aimed && tick - aimed < kRecent;
    const bool lapsed = restricted || !aimed || tick - aimed > kModeLapse;
    if (g_switch.update(aiming, lapsed, (frame.snapshot().buttons & kStickClick) != 0)) {
        g_descendMode.store(g_switch.descend, std::memory_order_release);
        __atomic_store_n(&g_promptDescend, g_switch.descend ? 1u : 0u, __ATOMIC_RELEASE);
        Logging.Log("[descend] mode %s", g_switch.descend ? "descend" : "ascend");
    }
    if (aiming) frame.maskOwnedButtons(kStickClick);
}

ClipperUpdate beforeClipperUpdate(void* clipper) {
#if ULTIMATE_ASCEND_SHRINE_RESTRICTED
    const bool shrine = reinterpret_cast<bool (*)()>(engine::imageBase() + off::kIsShrine)();
    g_inShrine.store(shrine, std::memory_order_release);
    if (shrine) {
        g_descendMode.store(false, std::memory_order_release);
        __atomic_store_n(&g_promptDescend, 0u, __ATOMIC_RELEASE);
        return {};
    }
#endif
    if (!g_descendMode.load(std::memory_order_acquire)) return {};
    const auto pending = reinterpret_cast<std::uintptr_t>(clipper) + off::clipper::kPending;
    const ClipperUpdate before{true, readMemory<std::uint32_t>(pending)};
    writeMemory<std::uint32_t>(pending, kSentinel);
    return before;
}

void beginMarkerPostCalc(void* clipper, void* context) {
    const auto actor = context ? readMemory<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(context) + 8) : 0;
    if (!totk::engine::isPlausibleAddress(actor)) return;
    g_scaleOriginY = g_descendMode.load(std::memory_order_relaxed)
        ? NAN : readVec(actor + off::actor::kPosition).y;
    g_scaleOwner.store(reinterpret_cast<std::uintptr_t>(clipper), std::memory_order_release);
}

void endMarkerPostCalc() { g_scaleOwner.store(0, std::memory_order_release); }

void scaleMarker(void* handle, const Vec3* position) {
    const auto owner = g_scaleOwner.load(std::memory_order_acquire);
    const auto slot = reinterpret_cast<std::uintptr_t>(handle) - owner;
    if (!owner || !position || slot < 60 || slot > 108 || (slot - 60) % 16) return;
    const float size = pure::upwardMarkerScale(position->y - g_scaleOriginY);
    const Vec3 scale{size, size, size};
    using SetScale = void (*)(void*, const Vec3*, const Vec3*);
    reinterpret_cast<SetScale>(engine::imageBase() + off::kMarkerPositionScale)(handle, position, &scale);
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
        {
            Hold hold(g_lock);
            if (g_event.active && g_event.actor != actor) {
                g_event.active = false;
                setOwned(0);
                forgetFall();
                Logging.Log("[descend] event cleared: live player changed");
            }
            if (g_event.actor == actor) g_event.link = live;
        }
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
    Vec3 localOrigin{};
    if (plan.valid() && plan.route == pure::Plan::Route::Object) {
        engine::BodySnapshot body;
        if (!engine::snapshotBody(plan.body, body)) plan.verdict = pure::Verdict::UnverifiedExit;
        else localOrigin = body.matrix.local(feet);
    }
    const bool valid = plan.valid();
    const bool depths = valid && plan.route == pure::Plan::Route::DeepTerrain &&
                        engine::depthsAt(pure::at(feet, plan.landing)) && !engine::depthsAt(feet);
    __atomic_store_n(&g_promptDescend, depths ? 2u : 1u, __ATOMIC_RELEASE);

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
        const auto fade = reinterpret_cast<void (*)(std::uintptr_t, std::uint32_t)>(engine::imageBase() + off::kFadeHandles);
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
        if (valid) { g_armed.route = plan.route; g_armed.body = plan.body; g_armed.localOrigin = localOrigin; }
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
    const auto pools = readMemory<std::uintptr_t>(engine::imageBase() + off::kEffectPools);
    const auto sizes = readMemory<std::uintptr_t>(engine::imageBase() + off::kEffectSizes);
    const auto event = readMemory<std::uintptr_t>(pools + bank * 8) +
                       readMemory<std::uintptr_t>(sizes + bank * 8) * index;
    return event && readMemory<std::uint32_t>(event + 32) == readMemory<std::uint32_t>(handle + 4) ? event : 0;
}

bool blendMarker(std::uintptr_t clipper, unsigned greenOffset, unsigned effect) {
    if (!warmMarker()) return false;
    // A previous real green marker may still have its ready loop. Hints stay silent.
    const auto sound = markerEvent(clipper + greenOffset + 8);
    if (sound && !(readMemory<std::uint8_t>(sound + 8) & 0x10))
        reinterpret_cast<void (*)(std::uintptr_t)>(engine::imageBase() + off::kFadeEvent)(sound);
    for (unsigned i = 0; i < 2; ++i) {
        const auto handle = clipper + greenOffset + i * 16;
        const auto event = markerEvent(handle);
        if (!event || (readMemory<std::uint8_t>(event + 8) & 0x10)) {
            // Native return is an indirect HandleSet through X8, despite its 16-byte payload.
            struct IndirectHandles { std::uint64_t effect, sound, unused; };
            const auto emit = reinterpret_cast<IndirectHandles (*)(std::uintptr_t, unsigned)>(
                engine::imageBase() + off::kEmitPowerEffect);
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
    const bool depths = fresh && __atomic_load_n(&g_promptDescend, __ATOMIC_ACQUIRE) == 2;
    if (warm || depths || g_blendOwner == clipper) {
        for (unsigned i = 0; i < 4; ++i) {
            const auto event = markerEvent(clipper + 60 + i * 16);
            if (!event) continue;
            const auto alpha = warm ? __atomic_load_n(i & 1 ? &g_cold : &g_warm, __ATOMIC_RELAXED)
                                    : depths && !(i & 1) ? 0x3F000000u : 0x3F800000u;
            writeMemory(event + 196, alpha);
            writeMemory(event + 420, readMemory<std::uint32_t>(event + 420) | 2u);
        }
        g_blendOwner = warm || depths ? clipper : 0;
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

// Keep the descent pose through the deep arrival delay; native preview, return and drop then resume normally.
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
        if (state > off::main_stage::kPreview ||
            !pure::sameBits(readVec(base + off::main_stage::kTarget), g_event.target)) {
            return false;
        }
        // Animation runs before the first preview timestamp is published by afterTravelState.
        if (state == off::main_stage::kPreview &&
            (g_event.route != pure::Plan::Route::DeepTerrain ||
             (g_event.previewTick && now() >= previewPoseTick(g_event)))) return false;
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

Vec3 preloadPosition(const Vec3& position, std::uintptr_t actor) {
    Hold hold(g_lock);
    return g_event.active && !g_event.released && (!actor || actor == g_event.actor) &&
                   g_event.route == pure::Plan::Route::DeepTerrain && pure::sameBits(position, g_event.target)
               ? g_event.drop : position;
}

void afterCameraTarget(void* component) {
    // This live component supplies its owner after the camera-target update, later than AI travel.
    const auto actor = engine::follow(reinterpret_cast<std::uintptr_t>(component), 24);
    Hold hold(g_lock);
    if (actor && g_event.active) {
        if (g_event.actor == actor) {
            g_event.link = readVec(actor + off::actor::kPosition);
            const auto attentionY = readMemory<float>(reinterpret_cast<std::uintptr_t>(component) + off::kCameraAttention + 4);
            if (std::fabs(attentionY - g_event.link.y) < pure::tuning::kCameraRange) {
                if (!g_event.previewTick) {
                    g_event.focusOffset = attentionY - g_event.link.y - pure::tuning::kTrackLook;
                    g_event.focusReady = true;
                }
                g_event.focusY = attentionY - g_event.focusOffset;
            } else g_event.focusReady = false;
        }
        else { g_event.active = false; setOwned(0); }
    }
}

TravelState beforeTravelState(void* node) {
    const auto actor = engine::nodeActor(node);
    const auto base = reinterpret_cast<std::uintptr_t>(node);
    const Vec3 target = readVec(base + off::main_stage::kTarget);
    std::uintptr_t player;
    Event event;
    {
        Hold hold(g_lock);
        if (!g_event.active || g_event.actor != actor || !pure::sameBits(target, g_event.target)) return {};
        player = g_event.player;
        event = g_event;
    }
    const auto state = readMemory<std::uint32_t>(base + off::main_stage::kState);
    bool holdNative = event.routeFailed;
    pure::Plan next;
    bool check = false;
    Vec3 origin = event.start;
    if (!event.routeFailed && event.route == pure::Plan::Route::DeepTerrain && !event.leaveTick &&
        (!event.exitValidated || (!event.exitRechecked &&
         std::fabs(readVec(actor + off::actor::kPosition).y - event.underside) <
             std::fmax(5.0f,readMemory<float>(base + off::main_stage::kSlowdown))))) {
        if (state == 1 && (!(readMemory<std::uint8_t>(base + 24) & 2) || !engine::preloadReady(event.target))) {
            // Native state 1 continues to own loading and cancellation.
        } else {
            pure::Plan candidate;
            candidate.route = event.route;
            candidate.floor = event.floor;
            candidate.underside = event.underside;
            candidate.drop = event.drop;
            next = engine::validateDeepExit(actor, candidate, event.exitValidated ? event.serial : 0);
            check = next.verdict != pure::Verdict::Pending;
            if (!check && std::fabs(readVec(actor + off::actor::kPosition).y - event.underside) < 1.5f) {
                engine::stopTravel(player);
                return {true,state,0,false,true};
            }
        }
    } else if (!event.routeFailed && event.route == pure::Plan::Route::Object && !event.leaveTick) {
        engine::BodySnapshot body;
        if (engine::snapshotBody(event.body, body)) origin = body.matrix.point(event.localOrigin);
        next = engine::movingPlan(actor, event.body, event.localOrigin);
        check = true;
    }
    if (check) {
        const bool support = next.valid() && engine::moveEventSupport(actor, next.target);
        const bool accepted = next.valid() && support;
        if (accepted) {
            writeVec(base + off::main_stage::kTarget, next.target);
            // Native preview motion follows its target only when this warp latch is set.
            if (state == off::main_stage::kPreview && next.route == pure::Plan::Route::Object)
                writeMemory<std::uint8_t>(base + 130, 1);
            if (next.route == pure::Plan::Route::Object)
                writeVec(player + off::player::kReturnTarget, pure::at(origin, origin.y + pure::tuning::kReturnLift));
            Hold lock(g_lock);
            if (g_event.serial == event.serial) {
                g_event.target = next.target;
                g_event.underside = next.underside;
                g_event.drop = next.drop;
                g_event.floor = next.floor;
                g_event.landing = next.landing;
                // Stationary terrain needs one final check, not a full survey every preview frame.
                if (event.exitValidated && event.route == pure::Plan::Route::DeepTerrain) {
                    g_event.exitRechecked = true;
                }
                if (next.route == pure::Plan::Route::Object) g_event.start = origin;
                if (!g_event.exitValidated) {
                    const auto behind = pure::horizontalDirection(g_event.previewPose);
                    g_event.previewPose = pure::previewPose(next.target,next.underside,
                        pure::chooseView(next.openViews,behind),behind);
                    g_event.orbit.begin(g_event.previewPose);
                    g_event.exitValidated = true;
                    Logging.Log("[descend] event %u deep exit loaded and collision-verified: underside_mm=%d",
                                event.serial,mm(next.underside));
                }
            }
        } else {
            // Preload readiness can precede physical collision availability. Hold under cover and retry.
            if (event.route == pure::Plan::Route::DeepTerrain && !event.exitValidated &&
                next.verdict == pure::Verdict::UnverifiedExit) {
                const auto tick = now();
                Hold lock(g_lock);
                if (!g_event.exitWaitTick) g_event.exitWaitTick = tick;
                if (tick - g_event.exitWaitTick < ticks(2000)) {
                    engine::stopTravel(player);
                    return {true,state,0,false,true};
                }
            }
            requestCancel(player,event.selector);
            setSwirl(event.serial,true,"destination changed; returning");
            holdNative = true;
            Hold lock(g_lock);
            if (g_event.serial == event.serial) {
                g_event.routeFailed = true;
                g_event.previewReady = false;
            }
            Logging.Log("[descend] event %u refused %s support=%d bottom=%d voxel=%d/%d/%d; returning",
                        event.serial,pure::verdictName(next.verdict),support,mm(next.oneSideY),
                        next.voxel.found,mm(next.voxel.firstSolid),mm(next.voxel.solidEnd));
        }
    }
    if (holdNative) {
        // getResult observes the cancel bit only in states 1-4, not the preview (5).
        writeMemory<std::uint32_t>(base + off::main_stage::kState, 1);
        engine::stopTravel(player);
    }
    return {true, state,
            readMemory<float>(base + off::main_stage::kPreviewTimer),
            (readMemory<std::uint8_t>(player + off::player::kCancelByte) & off::player::kCancelBit) != 0, holdNative};
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
    {
        Hold hold(g_lock);
        if (!g_event.active) return;
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
    if (firstPreview) {
        Logging.Log("[descend] event %u preview reached", event.serial);
    }
    if (state == kPreview && event.route == pure::Plan::Route::DeepTerrain && !event.exitRechecked) return;
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
    if (player != __atomic_load_n(&g_fallPlayer, __ATOMIC_RELAXED) || command != engine::imageBase() + off::kCommandSwirl)
        return command;
    __atomic_store_n(&g_fallTick, 0, __ATOMIC_RELAXED);
    return engine::imageBase() + off::kCommandFall;
}

// The A prompt's "Ascend" is message 0028 of LayoutMsg/DoCommand_00, and no game message says "Descend". While Descend aim is on, the found message becomes the raw text "Descend" with the raw-string id the game's own eui::MessageString::setMessage leaves; the prompt screen redraws once each time that id changes.
DESCEND_INTEGER_ONLY void onPromptMessage(std::uintptr_t message, std::uintptr_t label, bool setText) {
    using namespace off::prompt;
    const auto mode = __atomic_load_n(&g_promptDescend, __ATOMIC_ACQUIRE);
    if (!mode || !label) return;
    const auto* const name = reinterpret_cast<const volatile char*>(label);
    if (name[0] != '0' || name[1] != '0' || name[2] != '2' || name[3] != '8' || name[4]) return;
    if (setText) {
        *reinterpret_cast<volatile std::uintptr_t*>(message + kText) =
            reinterpret_cast<std::uintptr_t>(mode == 2 ? kDepthsText : kDescendText);
        *reinterpret_cast<volatile std::int32_t*>(message + kLength) = mode == 2 ? 6 : 7;
    }
    *reinterpret_cast<volatile std::uint64_t*>(message + kId) = kRawStringId - (mode == 2);
}

// This is a graphics-scene flag, not just an emitter switch. Native scene update, background and particle draws all consume it. Keep it through rendering; clearing it after effect calculation left the swirl with the ordinary world's render setup. Synchronize before both effect calculation and scene preparation, never around draw calls.
DESCEND_INTEGER_ONLY void synchronizeScreenCover() {
    using namespace off::ceiling_effect;
    const auto stamp = __atomic_load_n(&g_effectTick, __ATOMIC_ACQUIRE);
    const bool owned = __atomic_load_n(&g_ownedSerial, __ATOMIC_ACQUIRE) && stamp;
    if (!owned && !__atomic_load_n(&g_forcedCoverOwner, __ATOMIC_ACQUIRE)) return;
    const auto slot = *reinterpret_cast<const volatile std::uintptr_t*>(engine::imageBase() + kGlobalSlot);
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
        if (value & kInCeiling) return;
        *flags = value | kInCeiling;
        __atomic_store_n(&g_forcedCoverOwner, owner, __ATOMIC_RELEASE);
        return;
    }
    __atomic_store_n(&g_forcedCoverOwner, 0, __ATOMIC_RELEASE);
    if (value & kInCeiling) {
        *flags = value & ~kInCeiling;
    }
}

DESCEND_INTEGER_ONLY std::uint64_t effectDirection(std::uint64_t native) {
    const auto stamp = __atomic_load_n(&g_effectTick, __ATOMIC_ACQUIRE);
    if (!stamp) return native;
    if (!__atomic_load_n(&g_ownedSerial, __ATOMIC_ACQUIRE)) return native;
    // Main inherits Ascend's Up variant. Select Down while descending and Up while returning.
    return __atomic_load_n(&g_returnEffect, __ATOMIC_ACQUIRE) ? 1 : 0;
}

// After A the stage skips its wait for the swirl and moves Link down at a falling speed with no slowdown near the goal.
bool beforeCancelStage(void* node) {
    using namespace off::cancel_stage;
    const auto actor = engine::nodeActor(node);
    const auto base = reinterpret_cast<std::uintptr_t>(node);
    const auto tick = now();
    Event event;
    bool first = false, fallStarted = false;
    {
        Hold hold(g_lock);
        if (!g_event.active || g_event.actor != actor) return false;
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
    if (event.released) return false;
    if (!event.dropped) {
        if (event.route != pure::Plan::Route::Object) return false;
        // Once native motion has arrived or restored collision, retain its checked goal.
        if (readMemory<std::uint32_t>(base+kState) == kArrived ||
            !(readMemory<std::uint32_t>(event.player+off::player::kEventFlags) & off::player::kCollisionOff))
            return false;
        engine::BodySnapshot body;
        Vec3 origin = event.start;
        if (engine::snapshotBody(event.body,body)) origin = body.matrix.point(event.localOrigin);
        if (!engine::clearReturn(actor,origin)) {
            origin = event.start;
            if (!engine::clearReturn(actor,origin)) {
                bool clear = false;
                for (int up=1; up<=8; ++up) {
                    const auto candidate = pure::at(event.start,event.start.y+static_cast<float>(up));
                    if (engine::clearReturn(actor,candidate)) { origin=candidate; clear=true; break; }
                }
                if (!clear) {
                    engine::stopTravel(event.player);
                    if (!event.goalLogged) {
                        Hold lock(g_lock);
                        g_event.goalLogged=true;
                        Logging.Log("[descend] event %u return blocked by changed geometry; collision stays off",event.serial);
                    }
                    return true;
                }
            }
        }
        const auto goal = pure::at(origin,origin.y+pure::tuning::kReturnLift);
        if (!engine::moveEventSupport(actor,goal)) {
            engine::stopTravel(event.player);
            if (!event.goalLogged) {
                Hold lock(g_lock);
                g_event.goalLogged=true;
                Logging.Log("[descend] event %u return support unavailable",event.serial);
            }
            return true;
        }
        writeVec(base+kGoal,goal);
        writeVec(event.player+off::player::kReturnTarget,goal);
        {
            Hold lock(g_lock);
            if (g_event.serial == event.serial) g_event.start=origin;
        }
        return false;
    }
    if (fallStarted) {
        forgetFall();
        const auto state = readMemory<std::uint32_t>(base + kState);
        if (state == kSwirl) {
            // The game's own step when the swirl finishes, except that it enters a falling state directly.
            writeMemory<std::uint32_t>(base + kState, kFalling);
            writeMemory<std::uint64_t>(base + kClothWind, readMemory<std::uint64_t>(actor + off::actor::kClothWind));
            writeMemory<std::uint32_t>(base + kClothWind + 8,
                                       readMemory<std::uint32_t>(actor + off::actor::kClothWind + 8));
        }
        writeMemory<float>(base + kSlowdown, 0);
    }
    const float seconds = static_cast<float>(tick - event.fallTick) / static_cast<float>(engine::kTicksPerSecond);
    writeMemory<float>(base + kSpeed, pure::dropStep(seconds));
    return false;
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
    Logging.Log("[descend] event %u returned at %u ms: link_y_mm=%d", event.serial, ms, mm(link.y));
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
        }
    }
    Logging.Log("[descend] event %u released into a fall at %u ms: link_y_mm=%d",
                event.serial, ms, mm(link.y));
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
        const int index = cameraIndex(event, component, link, native, tick);
        if (index < 0 || !framePose(event, index, tick, link, native, pose, uncover) || !finite(pose)) return {};
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
    {
        Hold hold(g_cameraLock);
        const auto* const shown = recentAimPose(component, tick);
        if (!shown) return {};
        pose = shown->pose;
    }
    const CameraFade saved{true, readVec(component + kFadePosition), readVec(component + kFadeLookAt)};
    writeVec(component + kFadePosition, pose.position);
    writeVec(component + kFadeLookAt, pose.lookAt);
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
    {
        Hold hold(g_cameraLock);
        const auto* const shown = newestAimPose(tick);
        if (!shown) return {};
        pose = shown->pose;
    }
    const CameraFade saved{true, readVec(camera + kFadePosition), readVec(camera + kFadeLookAt)};
    if (!finite({saved.position, saved.lookAt}) || !pure::cameraInRange(saved.position, pose.position)) {
        return {};
    }
    writeVec(camera + kFadePosition, pose.position);
    writeVec(camera + kFadeLookAt, pose.lookAt);
    return saved;
}

void afterPlayerFade(void* cameraPointer, const CameraFade& saved) {
    if (!saved.saved) return;
    const auto camera = reinterpret_cast<std::uintptr_t>(cameraPointer);
    writeVec(camera + off::player_camera::kFadePosition, saved.position);
    writeVec(camera + off::player_camera::kFadeLookAt, saved.lookAt);
}

} // namespace descend::feature
