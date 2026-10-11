// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisActors.hpp"
#include "CryonisTarget.hpp"
namespace cryonis::engine {
Vec3 position(Handle actor);
Water sample(WorldContext world, float x, float z);
void scanWater(WorldContext world);
void roster(WorldContext world);
bool seen(WorldContext world, Handle actor);
std::uintptr_t body(Handle actor);
bool shadowOnly(Handle actor);
Riders captureRiders(WorldContext world, Vec3 base, float yaw);
void liftRiders(WorldContext world, Riders &riders, Vec3 base, float frame, bool finished,
                float yaw);
void move(Handle actor, Vec3 base, float frame, float yaw);
bool isSwimming(Handle actor);
bool spawnPillar(const Target &target);
void deleteActor(Handle actor);
void fixBody(std::uintptr_t body);
target::Group collisionGroup(Handle actor);
void modelInitialized(std::uintptr_t component);
}
