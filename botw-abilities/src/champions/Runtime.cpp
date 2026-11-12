// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include <atomic>
#include "Runtime.hpp"
#include "RuntimeFrame.hpp"
#include "Audio.hpp"
#include "AudioWork.hpp"
#include "Effects.hpp"
#include "Daruk.hpp"
#include "Fury.hpp"
#include "Hud.hpp"
#include "GaleNative.hpp"
#include "GaleRender.hpp"
#include "GameData.hpp"
#include "KeyItems.hpp"
#include "Locations.hpp"
#include "Mipha.hpp"
#include "SageProgress.hpp"
#include "totk/engine/ActorRoster.hpp"
#include "totk/port/CodeCheck.hpp"
#include "totk/engine/GameData.hpp"
#include "totk/engine/Npad.hpp"
#include "totk/engine/Raycast.hpp"
#include <lib.hpp>

namespace botw_abilities::champions {
namespace {
using namespace totk::engine;
using namespace totk::port::literals;
constexpr totk::port::Site kSites[]{
    {0x7EDC00, 0xF9400828, 0xB40002A8}, {0x2A267BC, 0xD102C3FF, 0xA9057BFD},
    {0x858590, 0xD10743FF, 0}, {0x69A030, 0xA9BE7BFD, 0xF9000BF3},
    {0x172F5D4, 0xA9BE7BFD, 0xA9014FF4},
};
std::uintptr_t g_base{};
std::atomic<std::uint64_t> g_clock{};
std::atomic<bool> g_running{};
std::atomic<std::uint64_t> g_context{};
std::atomic<std::uint64_t> g_contextGeneration{UINT64_MAX};
NpadReader g_input;
SceneTracker g_scene;
RaycastMailbox g_probe;
RaycastTicket g_ticket;
std::uint64_t g_previousClock{}, g_ticks{}, g_saveGeneration = UINT64_MAX;
std::uint8_t g_recharged{};
UseEvents<32> g_uses;
AudioQueue<32> g_sounds;
AudioQueue<4> g_announcements;
struct SoundSink {
    bool play(audio::Cue cue) { return audio::play(cue); }
    bool stop(audio::Cue cue) { return audio::stop(cue); }
} g_soundSink;

template <class Fn> Fn call(totk::port::Code offset) { return reinterpret_cast<Fn>(g_base + offset); }

bool contexts(std::array<Context, kBlessingCount>& output) {
    const auto scene = resolveGameDataManagerFromHolder(g_base + 0x462E180_v121);
    const auto gmd = resolveGameDataManagerFromHolder(g_base + 0x462E3D8_v121);
    if (!scene || !gmd) return false;
    // 1.4.3: the location list layout is unverified, so the castle check (recharge speed-up only) is off.
    const auto location = totk::port::is143() ? std::uintptr_t{0}
        : call<std::uintptr_t(*)(std::uintptr_t, int)>(0x172F5D4)(scene.value, 7);
    const auto castle = castleFromLocations(isPlausibleAddress(location) ? location : 0,
                                            [](std::uintptr_t address, void* out, std::size_t bytes) {
        if (!isPlausibleStringAddress(address) || bytes > (1ull << 40) - address) return false;
        std::memcpy(out, reinterpret_cast<const void*>(address), bytes);
        return true;
    });
    if (!castle) {
        static unsigned reports{};
        if (totk::port::is143()) {
            if (!reports++) Logging.Log("[champions] castle recharge speed-up off on 1.4.3: location list layout not verified");
        } else if (reports++ < 8) Logging.Log("[champions] CONTEXT_DEGRADED castle=unknown location=%u", unsigned(castle.error));
    }
    const bool shrine = call<bool(*)()>(0x69A030)();
    unsigned missing = 0;
    const auto obtained = [&](std::uint32_t hash) {
        std::uint8_t value{};
        const auto found = call<std::uint32_t(*)(std::uintptr_t, std::uint8_t*, std::uint32_t)>(
            0xD45810)(gmd.value, &value, hash) & 1;
        missing += found == 0;
        return found && value;
    };
    output = gameplayContexts(shrine, castle, [&](Blessing blessing) {
        return hasSageVow(blessing, obtained);
    });
    if (missing) {
        static unsigned reports{};
        if (reports++ < 8) Logging.Log("[champions] CONTEXT_DEGRADED vow_reads_missing=%u", missing);
    }
    return true;
}

void pause() {
    g_context.store(0, std::memory_order_release);
    g_probe.cancel();
    g_ticket = {};
    audio::update({});
    hud::hide();
}

void resetScene() {
    pause();
    g_recharged = 0;
    g_uses.clear();
    g_sounds.clear();
    g_announcements.clear();
    effects::reset();
    gale::reset();
    daruk::reset();
    fury_native::reset();
    hud::reset();
}

void publishContext(const SceneContext& scene, std::uint64_t generation,
                    const std::array<Context, kBlessingCount>& current) {
    if (g_contextGeneration.load(std::memory_order_relaxed) != generation) {
        g_context.store(0, std::memory_order_release);
        g_contextGeneration.store(generation, std::memory_order_release);
    }
    g_context.store(packContext(scene.token.value, current), std::memory_order_release);
}

void serviceUseEvents(std::uint64_t generation, std::uint64_t scene, const ActorHandle& player) {
    UseEvent event;
    while (g_uses.pop(generation, scene, event)) {
        if (event.blessing == Blessing::Daruk) daruk::impact(player);
        effects::used(event.blessing);
        for (const auto work : activationAudio(event.blessing, event.finalCharge))
            if (work && !g_sounds.push(work))
                Logging.Log("[champions] USED_AUDIO_OVERFLOW blessing=%u", unsigned(event.blessing));
    }
}

void serviceReadyAudio(float seconds) {
    g_sounds.tick(seconds);
    g_announcements.tickHead(seconds);
    for (std::size_t i = 0; i < kBlessingCount; ++i) {
        const auto blessing = static_cast<Blessing>(i);
        if ((g_recharged & (1u << i)) &&
            g_announcements.push(readyAudio(blessing,audio::kSeconds[unsigned(readyVoice(blessing))])))
            g_recharged &= std::uint8_t(~(1u << i));
    }
    auto dropped = g_sounds.service(g_soundSink);
    dropped |= g_announcements.service(g_soundSink);
    if (dropped)
        Logging.Log("[champions] AUDIO_DROPPED cues=0x%x attempts=%u", dropped, kAudioPlayAttempts);
}

void serviceSaveProbe(totk::core::SceneGeneration generation, ActorHandle player, std::uint64_t elapsed) {
    if (g_ticket.value) {
        const auto result = g_probe.poll(g_ticket, {g_ticks}, 12);
        if (result.status != RaycastPollStatus::Pending) {
            g_ticket = {};
            if (result.status == RaycastPollStatus::Ready && elapsed) {
                constexpr auto ready = GameDataWriteReadiness::LiveGameplayAndWorkerAlive;
                const auto written = data::flush(ready);
                if (written != StateTransfer::Ready)
                    Logging.Log("[champions] STATE_WRITE_DEFER result=%u", unsigned(written));
                items::service(ready, g_ticks);
            }
        }
    }
    if (!g_ticket.value && elapsed) {
        const auto position = readMemory<totk::core::WorldPosition>(
            player.address + layout::kActorPosition);
        auto end = position;
        end.y += 0.25f;
        const auto request = g_probe.begin({position, end, position, {8}, 0,
                                           {g_ticks}, generation});
        if (request) g_ticket = request.value;
    }
}

#ifdef TOTK_SUITE_MEMBER
std::atomic<bool> g_switchedOn{true};
bool g_offApplied{};
#endif

void tick(void* device) {
    const auto input = g_input.read(device);
    if (input.snapshot().freshSampleCount) ++g_ticks;
    const auto clock = g_clock.load(std::memory_order_acquire);
    const auto elapsed = clock - g_previousClock;
    g_previousClock = clock;
    const auto scene = g_scene.observe(resolveScene(g_base));
    const auto snapshot = data::state().snapshot();
    const bool changed = scene.observation != SceneObservation::SameScene ||
                         snapshot.generation != g_saveGeneration;
    g_saveGeneration = snapshot.generation;
    if (changed) resetScene();
    if (!scene.context.isReady() || !snapshot.ready || !g_running.load(std::memory_order_acquire)) {
        pause();
        return;
    }
    const auto player = resolvePlayerActors(scene.context);
    std::array<Context, kBlessingCount> current{};
    if (!player || !contexts(current)) {
        pause();
        return;
    }
#ifdef TOTK_SUITE_MEMBER
    if (g_switchedOn.load(std::memory_order_acquire)) {
        g_offApplied = false;
    } else {
        // Ends every ability; the effect pass still runs to remove leftover effect actors; nothing can start.
        if (!g_offApplied) {
            resetScene();
            audio::stopAll();
            g_offApplied = true;
            Logging.Log("[champions] SWITCHED_OFF abilities ended");
        }
        pause();
        effects::service(scene.context, player.value.player, 0);
        return;
    }
#endif
    publishContext(scene.context, snapshot.generation, current);
    const float seconds = simulationSeconds(elapsed, changed);
    const auto completed = data::state().tick(current, seconds);
    g_recharged |= completed;
    gale::injectLaunchJump(input);
    gale::service(scene.context, player.value.player, current[1], input.snapshot().buttons,
                  input.snapshot().freshSampleCount != 0, seconds);
    if (holdsMovement(gale::presentation().phase)) input.writeOwnedLeftStick(0, 0);
    pose::publish(scene.context, player.value.player);
    daruk::service(current[2], input.snapshot().held(1u << 8),
                   input.snapshot().freshSampleCount != 0);
    fury_native::service(scene.context, player.value.player, current[3]);
    serviceUseEvents(snapshot.generation, scene.context.token.value, player.value.player);
    hud::service(data::state().snapshot(), current);
    serviceReadyAudio(seconds);
    effects::service(scene.context, player.value.player, seconds);
    audio::update({.gameplay = true, .galeChannel = gale::active(),
                   .updraft = effects::updraftActive(), .furyChannel = fury_native::active(),
                   .galeReady = gale::presentation().phase == GalePhase::Ready});
    serviceSaveProbe(scene.generation, player.value.player, elapsed);
}

HOOK_DEFINE_TRAMPOLINE(Controller) {
    static void Callback(void* device) { Orig(device); tick(device); }
};

HOOK_DEFINE_TRAMPOLINE(Clock) {
    static void Callback(void* module, const void* pauseState, float frames) {
        Orig(module, pauseState, frames);
        const auto system = readMemory<std::uintptr_t>(g_base + 0x471DCB0_v121);
        const auto time = isPlausibleAddress(system) ? readMemory<std::uintptr_t>(system + 0xC8) : 0;
        const bool running = isPlausibleAddress(time) && !(readMemory<unsigned>(time + 0x18) & 1) &&
                             std::isfinite(frames) && frames > 0 && frames < 100;
        if (running) g_clock.fetch_add(std::uint64_t(double(frames) * 4294967296.0 + 0.5), std::memory_order_release);
        g_running.store(running, std::memory_order_release);
    }
};

// 1.4.3: the Clock hook's routine misses the gameplay frame path; this update runs every frame (scale at +16).
constexpr std::ptrdiff_t kFrameRateUpdate143 = 0x3E67F0_v143;
HOOK_DEFINE_TRAMPOLINE(FrameRate143) {
    static void Callback(void* rate, float rawFrames) {
        Orig(rate, rawFrames);
        const float frames = readMemory<float>(reinterpret_cast<std::uintptr_t>(rate) + 16);
        const auto system = readMemory<std::uintptr_t>(g_base + 0x471DCB0_v121);
        const auto time = isPlausibleAddress(system) ? readMemory<std::uintptr_t>(system + 0xC8) : 0;
        const bool running = isPlausibleAddress(time) && !(readMemory<unsigned>(time + 0x18) & 1) &&
                             std::isfinite(frames) && frames > 0 && frames < 100;
        if (running) g_clock.fetch_add(std::uint64_t(double(frames) * 4294967296.0 + 0.5), std::memory_order_release);
        g_running.store(running, std::memory_order_release);
    }
};
bool frameRateUpdateMatches(std::uintptr_t base) {
    const auto site = base + static_cast<std::uintptr_t>(kFrameRateUpdate143);
    const auto first = totk::suite::checkedCode(site), second = totk::suite::checkedCode(site + 4),
               store = totk::suite::checkedCode(site + 0x30);
    if (entryMatches(first, 0x6DBD23E9) && (second == 0xA9017BFD || chainedAbsoluteTail(site, first)) &&
        store == 0xBD001001) return true;
    Logging.Log("[champions] off on 1.4.3: the frame-rate update reads %08x,%08x store=%08x", first, second, store);
    return false;
}

HOOK_DEFINE_TRAMPOLINE(Raycast) {
    static std::uint64_t Thunk(const void* from, const void* to, const void* query, const void* out,
                               std::uint32_t mask, std::uint32_t flags) {
        return Orig(from, to, query, out, mask, flags);
    }
    static std::uint64_t Callback(const void* from, const void* to, const void* query, const void* out,
                                  std::uint32_t mask, std::uint32_t flags) {
        const auto result = Orig(from, to, query, out, mask, flags);
        g_probe.observe(Thunk, from, query);
        return result;
    }
};
} // namespace

#ifdef TOTK_SUITE_MEMBER
void switched(bool on) { g_switchedOn.store(on, std::memory_order_release); }
#endif

Context currentContext(Blessing blessing) {
    const auto packed = g_context.load(std::memory_order_acquire);
    const auto snapshot = data::state().snapshot();
    const auto scene = resolveScene(g_base);
    if (!snapshot.ready || snapshot.generation != g_contextGeneration.load(std::memory_order_acquire) ||
        !scene || scene.value.token.value != contextScene(packed) ||
        packed != g_context.load(std::memory_order_acquire)) return {};
    return unpackContext(packed, blessing);
}
std::uint64_t simulationTick() { return g_clock.load(std::memory_order_acquire); }

bool notifyUsed(UseEvent event) {
    const bool accepted = g_uses.push(event);
    if (!accepted) Logging.Log("[champions] USED_EVENT_OVERFLOW blessing=%u", unsigned(event.blessing));
    return accepted;
}

bool install(std::uintptr_t base) {
    if (!totk::port::codeMatches(base, kSites, "champions", "RUNTIME_BUILD_REFUSED")) return false;
    if (totk::port::is143() && !frameRateUpdateMatches(base)) return false;
    if (!hud::install(base) || !data::install(base) || !items::install(base) || !effects::install(base) ||
        !mipha::install(base) || !gale::install(base) || !pose::install(base) || !daruk::install(base) ||
        !fury_native::install(base)) return false;
    g_base = base;
    Controller::InstallAtOffset(0x2A267BC_v121);
    if (totk::port::is143()) FrameRate143::InstallAtOffset(kFrameRateUpdate143);
    else Clock::InstallAtOffset(0x7EDC00_v121);
    Raycast::InstallAtOffset(0x858590_v121);
    return true;
}
} // namespace botw_abilities::champions
