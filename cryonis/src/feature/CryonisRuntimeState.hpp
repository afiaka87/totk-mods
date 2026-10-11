// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisRuntime.hpp"
#include "CryonisInput.hpp"
#include "CryonisControl.hpp"
#include "CryonisCamera.hpp"
#include "CryonisActors.hpp"
#include "CryonisHooks.hpp"
#include "totk/engine/AbilityChoice.hpp"
#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/Npad.hpp"
#include "totk/ui/NativeFont.hpp"
namespace cryonis::runtime {
using engine::ActorRoster;
using engine::Handle;
using engine::Riders;
using engine::WaterGrid;
struct Pending {
    Target target;
    Reservation reservation;
    unsigned generation{};
    float age{};
    bool active{};
};
// Handles are non-owning; fresh roster identity and the scene token gate every reuse.
struct SceneState {
    totk::engine::SceneTracker tracker;
    totk::engine::SceneContext context;
    totk::engine::NpadReader controller;
    Handle player;
    ActorRoster roster;
    camera::Blend cameraBlend;
    void *terrain{};
    bool ready{};
};
struct PillarState {
    Handle actors[kPillarLimit];
    Riders riders[kPillarLimit], capturedRiders;
    Pending pending;
    bool endPlayed[kPillarLimit]{};
};
struct ControlsState {
    input::AimButtons ownedButtons;
    control::Memory memory;
    std::uint64_t buttons{}, previousClock{};
    unsigned ticks{}, seenSelections{};
    totk::engine::AbilityChoiceWatch otherAbilities;
};
struct TargetingState {
    target::Mailbox cast;
    Ray requestRay, validatedRay;
    Target validated, requested, preview;
    unsigned requestTick{}, validatedTick{};
};
struct PresentationState {
    Presentation frame;
    totk::ui::native_font::Mailbox<Presentation> renderMailbox, hudMailbox;
    float elapsedFrames{}, breakAge{-1};
    Vec3 broken;
};
using engine::HookSignals;
struct RuntimeState {
    State gameplay;
    SceneState scene;
    PillarState pillars;
    ControlsState controls;
    TargetingState targeting;
    WaterGrid water;
    PresentationState presentation;
    HookSignals signals;
};
extern RuntimeState runtimeState;
}
