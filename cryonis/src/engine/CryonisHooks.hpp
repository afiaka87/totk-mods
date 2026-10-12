// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisHookContext.hpp"
#include "CryonisTarget.hpp"
namespace cryonis::engine {
bool validate(std::uintptr_t base);
void publishPhysicsTime(HookContext hook, float frames);
void publishCameraRay(HookContext hook, Ray ray);
void observeRaycast(HookContext hook, target::Cast cast, const void *from, const void *query);
Ray cameraRay(HookContext hook);
void wheelOpened(HookContext hook, std::uintptr_t screen);
void wheelUpdated(HookContext hook, std::uintptr_t screen);
bool wheelCancelRequested(HookContext hook, std::uintptr_t screen);
void wheelExited(HookContext hook, std::uintptr_t screen, bool cancelled);
void nativeSelectionAccepted(HookContext hook, std::uintptr_t screen);
void maskRecentAbilityKey(HookContext hook, std::uintptr_t node);
std::uint64_t nativeAbilityAdmission(HookContext hook,
    std::uint64_t (*original)(std::uintptr_t), std::uintptr_t actor);
void cancelOwnedAmiibo(HookContext hook, std::uintptr_t node);
void wheelClosed(HookContext hook, std::uintptr_t screen);
void frameAimCamera(HookContext hook, std::uintptr_t state, float *output, float frames);
}
