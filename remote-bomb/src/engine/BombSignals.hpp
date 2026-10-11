// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <atomic>
#include <cstdint>

namespace remote_bomb::engine {
struct Signals {
    std::atomic<std::uint64_t> clock{}, wheel{};
    std::atomic<std::uintptr_t> wheelOwner{}, player{};
    std::atomic<unsigned> epoch{}, request{}, nativeSelections{};
    std::atomic<bool> running{}, remembered{};
};
bool validateRuntime(std::uintptr_t base);
void publishPhysicsTime(std::uintptr_t base, Signals &signals, float frames);
void wheelOpened(Signals &signals, std::uintptr_t screen);
void wheelUpdated(Signals &signals, std::uintptr_t screen);
void wheelExited(Signals &signals, std::uintptr_t screen, bool cancelled);
void wheelClosed(Signals &signals, std::uintptr_t screen);
bool wheelCancelRequested(const Signals &signals, std::uintptr_t screen);
void nativeSelectionAccepted(Signals &signals, std::uintptr_t screen);
std::uint64_t nativeAbilityAdmission(std::uint64_t (*original)(std::uintptr_t),
                                    const Signals &signals, std::uintptr_t actor);
void maskRecentAbilityKey(std::uintptr_t base, const Signals &signals, std::uintptr_t node);
void cancelOwnedAmiibo(std::uintptr_t base, const Signals &signals, std::uintptr_t node);
}
