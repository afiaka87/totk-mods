// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "StasisEngine.hpp"
#include "StasisEnemyHold.hpp"
#include "StasisPresentation.hpp"
#include "StasisShared.hpp"
#include "StasisTarget.hpp"
#include <atomic>
#include <cstdint>
namespace stasis::engine {
inline constexpr unsigned kHitCapacity = 32;
struct Hit {
    Vec3 normal, point, direction;
    std::uint32_t attack[4]{};
    std::uintptr_t attacker{}, victim{};
    int weapon{-1};
    std::uint32_t generation{};
};
struct HitQueue {
    Hit hits[kHitCapacity]{};
    unsigned count{};
};
struct CameraRequest {
    std::uintptr_t player{}, scene{};
    unsigned session{};
    bool aiming{};
};
// Everything the game's threads exchange. The input hook owns the hold and session fields; the
// other hooks publish the physics clock, wheel state, accepted hits and the camera ray.
struct Signals {
    std::atomic<std::uint64_t> wheel{}, clockTicks{};
    std::atomic<std::uintptr_t> wheelOwner{}, frozenActor{}, pausedActor{}, pausedAnimation{};
    std::atomic<std::uint32_t> wheelEpoch{}, wheelRequest{}, generation{1}, hitDrops{};
    std::atomic<unsigned> cameraSession{1}, motionRefused{};
    std::atomic<bool> clockRunning{};
    Shared<Ray> cameraRay;
    Shared<CameraRequest> cameraRequest;
    Shared<EnemyAnchor> enemyAnchor;
    Shared<HitQueue> hits;
    Shared<Presentation> presentation;
    target::Mailbox target;
};
extern Signals signals;

bool validate(std::uintptr_t base);
void wheelOpened(std::uintptr_t screen);
void wheelUpdated(std::uintptr_t screen);
bool wheelCancelRequested(std::uintptr_t screen);
void wheelExited(std::uintptr_t screen, bool cancelled);
void wheelClosed(std::uintptr_t screen);
void publishPhysicsTime(float frameScale);
void acceptedHit(void *record, void *victimComponent, void *attackInfo);
bool pauseAiNode(std::uintptr_t node);
bool pauseAnimation(std::uintptr_t controller);
void holdEnemyPhysics(void *component);
using UpdatePosition = std::uintptr_t (*)(std::uintptr_t, float *, float);
std::uintptr_t updateEnemyPosition(UpdatePosition original, std::uintptr_t actor, float *matrix,
                                   float frameScale);
void observeRaycast(target::Cast original, const void *from, const void *object);
void frameAimCamera(std::uintptr_t state, float *view, float frameScale);
inline bool presentation(Presentation &out) { return signals.presentation.tryRead(out); }
inline void publishCameraRay(Ray ray) { signals.cameraRay.tryWrite(ray); }
} // namespace stasis::engine
