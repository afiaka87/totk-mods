// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisPresentation.hpp"
#include "CryonisLift.hpp"
#include "totk/engine/ActorRoster.hpp"
namespace cryonis::engine {
// Handles come from the roster; null denotes absence and scene/name identity gates tick reuse.
using Handle = totk::engine::ActorHandle;
inline constexpr const char *kActor = "Obj_CryonisPillar";
inline constexpr unsigned kRiderLimit = 32;
struct Rider {
    Handle actor;
    lift::Bounds relative;
    Vec3 requested;
    bool seen{}, hasRequest{};
};
struct Riders {
    Rider items[kRiderLimit];
    unsigned count{};
};
struct ActorRoster {
    Handle actors[4096], candidates[8];
    unsigned count{}, candidateCount{};
};
struct WaterGrid {
    float heights[2][kWaterSide * kWaterSide];
    Vec3 origins[2];
    unsigned front{}, cursor{};
};
// Borrowed context never escapes an adapter call or retains engine object ownership.
struct WorldContext {
    const totk::engine::SceneContext &scene;
    Handle player;
    ActorRoster &roster;
    void *&terrain;
    Riders *riders;
    Riders &capturedRiders;
    WaterGrid &water;
};
}
