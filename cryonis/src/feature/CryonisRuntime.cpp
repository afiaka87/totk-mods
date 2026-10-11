// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisRuntime.hpp"
#include "CryonisRuntimeState.hpp"
#include "CryonisFeature.hpp"
#include "CryonisEngine.hpp"
#include "CryonisAudio.hpp"
#include "totk/engine/AbilityChoice.hpp"
#include <lib.hpp>
#include <cstring>
namespace cryonis::runtime {
using namespace engine;
namespace eng = totk::engine;
RuntimeState runtimeState;
WorldContext world() {
    auto &scene = runtimeState.scene;
    return {scene.context,
            scene.player,
            scene.roster,
            scene.terrain,
            runtimeState.pillars.riders,
            runtimeState.pillars.capturedRiders,
            runtimeState.water};
}
HookContext hooks() {
    return {runtimeState.signals, runtimeState.scene.cameraBlend, runtimeState.targeting.cast};
}
void clearTarget(bool clearRequested) {
    auto &targeting = runtimeState.targeting;
    targeting.validated = {};
    targeting.preview = {};
    if (clearRequested)
        targeting.requested = {};
    targeting.cast.cancel();
}

namespace {
void clearActivation() {
    runtimeState.controls.memory.activation.reset();
    runtimeState.signals.request.store(0);
}
bool refreshScene() {
    auto &scene = runtimeState.scene;
    const auto observed = scene.tracker.observe(eng::resolveScene(moduleBase));
    if (!observed.context.isReady()) {
        if (scene.ready)
            reset("world unavailable");
        return false;
    }
    if (scene.context.token != observed.context.token)
        reset("scene change");
    scene.context = observed.context;
    const auto player = eng::resolvePlayerActors(scene.context);
    if (!player) {
        if (scene.ready)
            reset("player unavailable");
        return false;
    }
    scene.player = player.value.player;
    scene.ready = true;
    runtimeState.signals.keyPlayer.store(scene.player.address, std::memory_order_release);
    return true;
}
void exitSwimming(bool swimming, bool running) {
    if (!running || !swimming || !runtimeState.gameplay.aiming)
        return;
    runtimeState.gameplay.aiming = false;
    clearTarget(true);
    clearActivation();
    runtimeState.controls.memory.recent.cancelTap();
    Logging.Log("[cryonis] AIM_EXIT swimming=1");
}
void advanceSimulation(float frames, bool running) {
    roster(world());
    runtimeState.gameplay.tick(frames, running);
    if (!running)
        return;
    auto &presentation = runtimeState.presentation;
    presentation.elapsedFrames += frames;
    updatePillars(frames);
    if (presentation.breakAge >= 0)
        presentation.breakAge += frames;
}
void acceptNativeSelection() {
    auto &signals = runtimeState.signals;
    const auto selections = signals.nativeSelections.load(std::memory_order_acquire);
    const bool otherChosen =
        runtimeState.controls.otherAbilities.chosenElsewhere(eng::Ability::Cryonis);
    if (selections == runtimeState.controls.seenSelections && !otherChosen)
        return;
    runtimeState.controls.seenSelections = selections;
    runtimeState.controls.memory.recent.nativeAccepted();
    runtimeState.gameplay.aiming = false;
    clearTarget(true);
    clearActivation();
    signals.recentKeyOwned.store(false);
    signals.recentNativeOwned.store(false, std::memory_order_release);
    Logging.Log("[cryonis] NATIVE_ABILITY_TAKEOVER preview_cleared=1 mod=%u", unsigned(otherChosen));
}
control::Transition processControls(const control::Context &context) {
    auto &signals = runtimeState.signals;
    auto &gameplay = runtimeState.gameplay;
    const auto result = control::transition(runtimeState.controls.memory, context);
    runtimeState.controls.memory = result.next;
    signals.recentKeyOwned.store(result.next.recent.tap, std::memory_order_release);
    signals.request.store(result.next.activation.pendingEpoch, std::memory_order_release);
    if (result.chord.event == input::Event::Activated)
        eng::chooseAbility(eng::Ability::Cryonis);
    if (result.chord.event != input::Event::None)
        Logging.Log("[cryonis] WHEEL_ACTION event=%u epoch=%u", unsigned(result.chord.event),
                    context.wheel.epoch);
    if (context.swimming && context.fresh && (context.pressed & input::L) &&
        result.next.recent.remembered)
        Logging.Log("[cryonis] AIM_REFUSED swimming=1 recent=1");
    switch (result.aim) {
    case control::AimChange::Exit:
        gameplay.aiming = false;
        clearTarget(true);
        Logging.Log("[cryonis] AIM_EXIT L pending=%u",
                    unsigned(runtimeState.pillars.pending.active));
        break;
    case control::AimChange::Enter:
        gameplay.aiming = true;
        gameplay.previewFrames = 0;
        runtimeState.targeting.validated = {};
        runtimeState.targeting.preview = {};
        Logging.Log("[cryonis] AIM_BEGIN recent=%u", unsigned(result.quick));
        break;
    case control::AimChange::None:
        break;
    }
    signals.recentNativeOwned.store(result.next.recent.remembered, std::memory_order_release);
    return result;
}
void applyTargetAction(bool running, std::uint64_t pressed, bool chordEntered) {
    const auto pending = runtimeState.pillars.pending.active;
    const auto &validated = runtimeState.targeting.validated;
    switch (control::action(runtimeState.gameplay.aiming, running, pressed, chordEntered, pending,
                            validated)) {
    case control::Action::Cancel:
        runtimeState.gameplay.aiming = false;
        clearTarget(false);
        Logging.Log("[cryonis] AIM_EXIT B pending=%u", unsigned(pending));
        break;
    case control::Action::CreationPending:
        Logging.Log("[cryonis] ACTION_REFUSED creation pending");
        break;
    case control::Action::Shatter:
        erase(unsigned(validated.pillar), "manual shatter");
        clearTarget(false);
        break;
    case control::Action::Construct:
        requestSpawn();
        break;
    case control::Action::None:
        break;
    }
}
void publishFrame(bool running) {
    auto &signals = runtimeState.signals;
    const auto &gameplay = runtimeState.gameplay;
    const auto &water = runtimeState.water;
    auto &presentation = runtimeState.presentation;
    signals.aiming.store(gameplay.aiming);
    signals.cameraPlayer.store(runtimeState.scene.player.address);
    signals.cameraScene.store(runtimeState.scene.context.token.value);
    auto &frame = presentation.frame;
    frame = {};
    std::memcpy(frame.pillars, gameplay.pillars, sizeof(frame.pillars));
    std::memcpy(frame.water, water.heights[water.front], sizeof(frame.water));
    frame.waterOrigin = water.origins[water.front];
    frame.target = runtimeState.targeting.preview;
    frame.canPlace = runtimeState.targeting.validated.valid;
    frame.aiming = gameplay.aiming;
    frame.running = running;
    frame.pending = runtimeState.pillars.pending.active;
    frame.cooldown = gameplay.cooldown;
    frame.previewFrames = gameplay.previewFrames;
    frame.elapsedFrames = presentation.elapsedFrames;
    frame.breakAge = presentation.breakAge;
    frame.broken = presentation.broken;
    frame.player = position(runtimeState.scene.player);
    frame.sceneReady = true;
    presentation.renderMailbox.publish(frame);
    presentation.hudMailbox.publish(frame);
}
}
void reset(const char *reason) {
    Logging.Log("[cryonis] RESET %s", reason);
    audio::preview(false);
    auto &scene = runtimeState.scene;
    auto &signals = runtimeState.signals;
    auto &water = runtimeState.water;
    auto &presentation = runtimeState.presentation;
    runtimeState.gameplay.reset();
    scene.player = {};
    scene.context = {};
    scene.roster.count = 0;
    scene.terrain = nullptr;
    scene.ready = false;
    runtimeState.pillars = {};
    clearTarget(true);
    runtimeState.targeting.validatedRay = {};
    runtimeState.controls.memory.recent.cancelTap();
    clearActivation();
    runtimeState.controls.ownedButtons = {};
    signals.recentKeyOwned.store(false);
    signals.recentNativeOwned.store(false);
    signals.keyPlayer.store(0);
    signals.cameraPlayer.store(0);
    signals.cameraScene.store(0);
    signals.aiming.store(false);
    ++signals.generation;
    water.cursor = 0;
    water.front = 0;
    for (auto &grid : water.heights)
        for (auto &height : grid)
            height = kNoWater;
    presentation.breakAge = -1;
    presentation.frame = {};
    presentation.renderMailbox.publish(presentation.frame);
    presentation.hudMailbox.publish(presentation.frame);
}
void initialize(std::uintptr_t base) {
    moduleBase = base;
    reset("module start");
}
bool presentation(Presentation &out, bool hud) {
    auto &state = runtimeState.presentation;
    out = hud ? state.hudMailbox.consume() : state.renderMailbox.consume();
    return out.sceneReady;
}
void tick(void *device) {
    auto &controls = runtimeState.controls;
    auto &gameplay = runtimeState.gameplay;
    const auto frame = runtimeState.scene.controller.read(device);
    const auto buttons = frame.snapshot().buttons;
    const bool fresh = frame.snapshot().freshSampleCount != 0;
    const auto pressed = fresh ? buttons & ~controls.buttons : 0;
    if (fresh) {
        controls.buttons = buttons;
        ++controls.ticks;
    }
    const auto clock = runtimeState.signals.clock.load(std::memory_order_acquire);
    const bool running = runtimeState.signals.running.load(std::memory_order_acquire);
    const float frames = float(double(clock - controls.previousClock) / 4294967296.0);
    controls.previousClock = clock;
    if (!refreshScene())
        return;
    const bool swimming = isSwimming(runtimeState.scene.player);
    exitSwimming(swimming, running);
    advanceSimulation(frames, running);
    acceptNativeSelection();
    const auto wheel = input::unpack(runtimeState.signals.wheel.load(std::memory_order_acquire));
    const auto chord = processControls({wheel, buttons, pressed, fresh, running, swimming,
                                        gameplay.aiming, runtimeState.pillars.pending.active,
                                        eng::lHeldElsewhere(eng::Ability::Cryonis)})
                           .chord;
    // Capture ownership before any exit so held A cannot become native Climb down.
    const auto owned = chord.mask | controls.ownedButtons.mask(gameplay.aiming, buttons, fresh);
    if (gameplay.aiming && running) {
        select();
        if (fresh)
            scanWater(world());
    }
    applyTargetAction(running, pressed, chord.beginAim);
    frame.maskOwnedButtons(owned);
    frame.addOwnedButtons(chord.add);
    audio::preview(gameplay.aiming && running);
    publishFrame(running);
}
}
