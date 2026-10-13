// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombState.hpp"
#include "BombEffects.hpp"
#include "NativeBombActions.hpp"
#include "BombHighlight.hpp"
#include <array>

namespace remote_bomb::runtime {
struct BombView {
    engine::Position position;
    std::array<float, 9> rotation{};
    bool live{};
    highlight::Level highlight{};
};
struct Presentation {
    State state;
    std::array<BombView, 2> bombs{};
    effects::Timeline effects;
    bool available{}, remembered{}, running{};
    std::uint64_t clock{};
};
bool initialize(std::uintptr_t base);
void tick(void *device);
void publishPhysicsTime(float frames);
void wheelOpened(std::uintptr_t screen);
void wheelExited(std::uintptr_t screen, bool cancelled);
void wheelClosed(std::uintptr_t screen);
void nativeSelectionAccepted(std::uintptr_t screen);
std::uint64_t nativeAbilityAdmission(std::uint64_t (*original)(std::uintptr_t), std::uintptr_t actor,
                                    std::uintptr_t caller);
void maskRecentAbilityKey(std::uintptr_t node);
void cancelOwnedAmiibo(std::uintptr_t node);
bool presentation(Presentation &out);
}
