// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace remote_bomb::engine::explosion {
using ContactCalculation = void (*)(std::uintptr_t, std::uintptr_t, void *);
bool initialize(std::uintptr_t base);
void sceneChanged(unsigned generation);
void publishTime(std::uint64_t clock, bool running);
void publishPlayer(std::uintptr_t actor);
void elementPhysicsPrepared(std::uintptr_t element, std::uintptr_t context);
void reportChemicalDamage(std::uintptr_t info);
bool admitElementListClear(std::uintptr_t list);
bool admitDeathProcedure(std::uintptr_t component);
void instanceCreated(std::uintptr_t instance);
void instanceRemoved(std::uintptr_t instance);
void calculateContact(ContactCalculation original, std::uintptr_t contact,
                      std::uintptr_t calculation, void *output);
}
