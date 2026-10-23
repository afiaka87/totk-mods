// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "GaleNative.hpp"
#include "GaleResources.hpp"
#include "GaleMotion.hpp"
#include "GalePoseStorage.hpp"
#include "GameData.hpp"
#include "Effects.hpp"
#include "GaleGlide.hpp"
#include "Runtime.hpp"
#include "totk/engine/CodeCheck.hpp"
#include "totk/engine/Transform.hpp"
#include <bit>
#include <lib.hpp>

namespace botw_abilities::champions::gale {
namespace {
using namespace totk::engine;
constexpr CodeSite kSites[]{
    {0x107E8BC, 0xA9BE7BFD, 0xA9014FF4}, {0xBC7610, 0xF9400408, 0},
    {0x11BD100, 0xF9411408, 0xB4000068},
    {0xB7D960, 0xA9BE7BFD, 0xA9014FF4}, {0xA04560, 0xA9BF7BFD, 0x910003FD},
    {0x1722210, 0xD10203FF, 0}, {0x1D6E54C, 0xFC1B0FE8, 0},
    {0xB60C00, 0xD100C3FF, 0xA9027BFD},
};
std::uintptr_t g_base{};
std::atomic<std::uint64_t> g_jumpEpoch{};
std::atomic<std::uint64_t> g_presentation{};
std::atomic<std::uint32_t> g_channelEpoch{1};
std::uint64_t g_seenJump{}, g_buttons{};
Gale g_state;
GaleGlide g_glide;
bool g_glideForced{}, g_grounded = true;
std::atomic_flag g_glideLock{};
struct GlideGuard {
    GlideGuard() { while (g_glideLock.test_and_set(std::memory_order_acquire)) {} }
    ~GlideGuard() { g_glideLock.clear(std::memory_order_release); }
};
template <class Fn> Fn call(std::uintptr_t offset) { return reinterpret_cast<Fn>(g_base + offset); }

bool launch(const SceneContext& scene, ActorHandle player, std::uintptr_t component, const Context& context) {
    const auto transforms = TransformService(TransformFunctions::fromMainBase(g_base));
    const auto body = galeLaunchBody(component, player.address, isPlausibleAddress,
        [](std::uintptr_t address) { return readMemory<std::uintptr_t>(address); });
    const auto placement = transforms.read(player, scene.token);
    if (!body || !placement || readMemory<std::uintptr_t>(component + 24) != player.address) {
        Logging.Log("[champions] GALE_LAUNCH_REFUSED reason=player-physics");
        return false;
    }
    const auto snapshot = data::state().snapshot();
    const auto claim = data::state().reserve(snapshot.generation, Blessing::Revali, context);
    if (!claim) {
        Logging.Log("[champions] GALE_LAUNCH_REFUSED reason=charge refusal=%u", unsigned(claim.use.refusal));
        return false;
    }
    auto anchor = placement.value;
    anchor.rotation = {{1,0,0,0,1,0,0,0,1}};
    const bool accepted = effects::launchUpdraft(anchor);
    const bool committed = data::state().finish(claim, accepted);
    if (!accepted || !committed) {
        Logging.Log("[champions] GALE_LAUNCH_REFUSED field=%u committed=%u", unsigned(accepted), unsigned(committed));
        return false;
    }
    { GlideGuard guard; g_glide.arm({player.address,scene.token.value,claim.generation}); g_glideForced = false; }
    notifyUsed({claim.generation, scene.token.value, Blessing::Revali, claim.use.state.charges == 0});
    Logging.Log("[champions] GALE_LAUNCH charges=%u jump=queued", unsigned(claim.use.state.charges));
    return true;
}
constexpr std::uint64_t kJumpButton = 1u << 2;

HOOK_DEFINE_TRAMPOLINE(Jump) {
    static std::uintptr_t Callback(std::uintptr_t action) {
        const auto actor = call<std::uintptr_t(*)(std::uintptr_t)>(0xBC7610)(action);
        const auto result = Orig(action);
        const auto name = isPlausibleAddress(actor) ? readMemory<const char*>(actor + layout::kActorNamePointer) : nullptr;
        if (isPlausibleStringAddress(reinterpret_cast<std::uintptr_t>(name)) && std::strcmp(name, "Player") == 0)
            g_jumpEpoch.fetch_add(1, std::memory_order_release);
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(GlideEntry) {
    static std::uint64_t Callback(std::uintptr_t actor, float clearance) {
        const auto native = Orig(actor,clearance);
        if (native & 1) return native;
        const auto scene = resolveScene(g_base);
        const auto context = currentContext(Blessing::Revali);
        if (!scene || !context.gameplay || context.shrine) return native;
        const auto generation = data::state().snapshot().generation;
        const bool ownsGlider = call<bool(*)(std::uint32_t)>(0xB60C00)(1274277390);
        bool forced, first;
        {
            GlideGuard guard;
            forced = g_glide.admits({actor,scene.value.token.value,generation},context.gameplay,context.shrine,ownsGlider);
            first = forced && !g_glideForced;
            g_glideForced |= forced;
        }
        if (first) Logging.Log("[champions] GALE_GLIDE_FORCED native=0 owns_glider=1");
        return forced ? 1 : native;
    }
};
HOOK_DEFINE_TRAMPOLINE(GlideEntered) {
    static std::uint64_t Callback(std::uintptr_t action) {
        const auto result = Orig(action);
        const auto actor = call<std::uintptr_t(*)(std::uintptr_t)>(0xBC7610)(action);
        bool accepted;
        { GlideGuard guard; accepted = g_glide.entered(actor); }
        if (accepted) Logging.Log("[champions] GALE_GLIDE_ENTER");
        return result;
    }
};
} // namespace

void reset() {
    { GlideGuard guard; g_glide.reset(); }
    g_state = {}; g_buttons = 0; g_grounded = true;
    g_presentation.store(0, std::memory_order_release);
    g_seenJump = g_jumpEpoch.load(std::memory_order_acquire);
    effects::hold(Effect::RevaliAura, false);
    effects::hold(Effect::RevaliGust, false);
}
bool active() { return channeling(presentation().phase); }
Gale presentation() {
    const auto word = g_presentation.load(std::memory_order_acquire);
    return {static_cast<GalePhase>((word >> 32) & 0xFF), std::bit_cast<float>(std::uint32_t(word)), bool(word & (1ull << 40))};
}
std::uint32_t channelEpoch() { return g_channelEpoch.load(std::memory_order_acquire); }

namespace {
struct Motion {
    std::uintptr_t component = 0;
    bool grounded = false, stopped = false, disrupted = false;
    const char* refusal = nullptr;
};

Motion readMotion(const ActorHandle& player) {
    const auto component = call<std::uintptr_t(*)(std::uintptr_t)>(0x11BD100)(player.address);
    if (!isPlausibleAddress(component)) return {.refusal = "component"};
    const auto registry = readMemory<std::uintptr_t>(player.address + layout::kActorComponentRegistry);
    if (!isPlausibleAddress(registry) || !isPlausibleAddress(readMemory<std::uintptr_t>(registry + 760)) ||
        !isPlausibleAddress(readMemory<std::uintptr_t>(registry + 424))) return {.refusal = "life-condition"};
    const auto velocity = readMemory<totk::core::LinearVelocity>(player.address + layout::kActorLinearVelocity);
    const bool finite = std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z);
    const bool grounded = (readMemory<unsigned>(component + 0xE50) & 1) != 0;
    const bool stopped = finite && velocity.x*velocity.x + velocity.z*velocity.z < 0.0225f;
    const bool swimming = call<bool(*)(std::uintptr_t)>(0xA04560)(player.address);
    const bool damaged = call<bool(*)(std::uintptr_t)>(0xB7D960)(player.address);
    return {component, grounded, stopped, !finite || swimming || damaged};
}

// The glide claims the jump injected after a launch, so only the player's own jumps count.
bool playerJumped() {
    const auto epoch = g_jumpEpoch.load(std::memory_order_acquire);
    if (epoch == g_seenJump) return false;
    g_seenJump = epoch;
    bool own;
    { GlideGuard guard; own = g_glide.claimJump(); }
    if (own) Logging.Log("[champions] GALE_JUMP_SEEN");
    return !own;
}

void publish(const Gale& state) {
    g_presentation.store(std::uint64_t(std::bit_cast<std::uint32_t>(state.elapsedSeconds)) |
        (std::uint64_t(state.phase) << 32) | (std::uint64_t(state.airborneSeen) << 40), std::memory_order_release);
}
} // namespace

void service(const SceneContext& scene, const ActorHandle& player, Context context,
             std::uint64_t buttons, bool fresh, float seconds) {
    if (fresh) g_buttons = buttons;
    const auto motion = readMotion(player);
    if (motion.refusal) {
        reset();
        Logging.Log("[champions] GALE_FRAME_REFUSED reason=%s", motion.refusal);
        return;
    }
    g_grounded = motion.grounded;
    constexpr std::uint64_t kInterruptButtons = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9);
    const bool jump = playerJumped();
    const auto snapshot = data::state().snapshot();
    if (!snapshot.ready) return;
    bool glideExpired;
    { GlideGuard guard; glideExpired = g_glide.advance({player.address,scene.token.value,snapshot.generation},seconds); }
    if (glideExpired) Logging.Log("[champions] GALE_GLIDE_REFUSED reason=timeout");
    const auto step = stepGale(g_state, snapshot.states[1], context,
        {jump, (g_buttons & kJumpButton) != 0, motion.grounded, motion.stopped,
         motion.disrupted || (active() && (g_buttons & kInterruptButtons)), seconds}, kGaleChannelSeconds);
    if (step.beginChannel && !pose::load()) {
        reset();
        Logging.Log("[champions] GALE_CHANNEL_REFUSED reason=pose-asset");
        return;
    }
    if (step.beginChannel) g_channelEpoch.fetch_add(1, std::memory_order_release);
    g_state = step.state;
    publish(g_state);
    if (step.beginChannel) {
        effects::pulse(Effect::RevaliArrive, 1.2f);
        effects::pulse(Effect::RevaliFlare, 1.5f);
        effects::hold(Effect::RevaliAura, true);
        effects::hold(Effect::RevaliGust, true);
        Logging.Log("[champions] GALE_CHANNEL_BEGIN");
    }
    if (step.becameReady) Logging.Log("[champions] GALE_READY");
    if (step.endChannel) {
        effects::hold(Effect::RevaliAura, false);
        effects::hold(Effect::RevaliGust, false);
        Logging.Log("[champions] GALE_CHANNEL_END launch=%u", unsigned(step.launch));
    }
    if (step.launch) launch(scene, player, motion.component, context);
    else if (jump && admission(snapshot.states[1], context) != Refusal::None)
        Logging.Log("[champions] GALE_JUMP_REFUSED refusal=%u", unsigned(admission(snapshot.states[1], context)));
}

// Runs before service so the release reaches the game before the injected press; grounding is last tick's.
void injectLaunchJump(const NpadFrame& input) {
    const auto& sample = input.snapshot();
    GlideSample inject;
    int attempt;
    {
        GlideGuard guard;
        inject = g_glide.injectJump(sample.freshSampleCount != 0, (sample.buttons & kJumpButton) != 0, g_grounded);
        attempt = inject.started == GlidePulse::Glide ? g_glide.glidePresses : g_glide.jumpPresses;
    }
    if (!inject.press) return;
    input.addOwnedButtons(kJumpButton);
    if (inject.started == GlidePulse::Jump) Logging.Log("[champions] GALE_JUMP_PULSE attempt=%d", attempt);
    if (inject.started == GlidePulse::Glide) Logging.Log("[champions] GALE_GLIDE_PULSE attempt=%d", attempt);
}

bool install(std::uintptr_t base) {
    if (!codeMatches(base, kSites, "champions", "GALE_BUILD_REFUSED")) return false;
    g_base = base;
    Jump::InstallAtOffset(0x107E8BC);
    GlideEntry::InstallAtOffset(0x1722210);
    GlideEntered::InstallAtOffset(0x1D6E54C);
    return true;
}
} // namespace botw_abilities::champions::gale
