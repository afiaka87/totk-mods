// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "BombReference.hpp"

namespace remote_bomb {
enum class Phase { Ready, Creating, Live, DetonationRequested, Recharging, Stored };
enum class Effect { None, Create, Detonate };
enum class Event { None, UseSelected, SelectSphere, SelectCube, CreationFailed, Exploded, DetonationFailed,
                   ActorLost, SceneChanged, Provisioned, InventoryActorCreated, StockLost, StockReplaced };

struct Ticket {
    std::uint32_t scene{}, serial{};
    friend constexpr bool operator==(Ticket, Ticket) = default;
};
struct Slot {
    Phase phase = Phase::Ready;
    Ticket ticket{};
    float rechargeSeconds{};
};
struct State {
    std::array<Slot, 2> slots{};
    Shape selected = Shape::Sphere;
    Upgrade upgrade = Upgrade::Plus;
    std::uint32_t scene{}, nextSerial = 1;
};
struct Input {
    Event event{};
    Shape shape{};
    Ticket ticket{};
    std::uint32_t scene{};
};
struct Command {
    Effect effect{};
    Shape shape{};
    Ticket ticket{};
};
struct Transition {
    State next;
    Command command;
};

inline constexpr unsigned index(Shape shape) { return shape == Shape::Sphere ? 0u : 1u; }
// Quick L targets the selected form while its bomb is out, otherwise the other form's live bomb, so a bomb left out after switching forms can always be set off.
inline Shape detonationTarget(const State &state) {
    const auto other = state.selected == Shape::Sphere ? Shape::Cube : Shape::Sphere;
    const auto phase = state.slots[index(state.selected)].phase;
    if (phase != Phase::Live && phase != Phase::DetonationRequested &&
        state.slots[index(other)].phase == Phase::Live)
        return other;
    return state.selected;
}
inline State advanceRecharge(State previous, float gameplaySeconds) {
    if (!std::isfinite(gameplaySeconds) || gameplaySeconds <= 0.f)
        return previous;
    for (auto &slot : previous.slots) {
        if (slot.phase != Phase::Recharging)
            continue;
        slot.rechargeSeconds = std::max(0.f, slot.rechargeSeconds - gameplaySeconds);
        if (slot.rechargeSeconds == 0.f)
            slot.phase = Phase::Ready;
    }
    return previous;
}

inline Transition transition(State previous, Input input) {
    Transition result{previous, {}};
    auto &next = result.next;
    if (input.event == Event::SceneChanged) {
        if (next.scene != input.scene) {
            next.scene = input.scene;
            next.slots = {};
        }
        return result;
    }
    if (input.event == Event::SelectSphere || input.event == Event::SelectCube) {
        next.selected = input.event == Event::SelectSphere ? Shape::Sphere : Shape::Cube;
        return result;
    }
    const auto shape = input.event == Event::UseSelected ? next.selected : input.shape;
    auto &slot = next.slots[index(shape)];
    if (input.event == Event::InventoryActorCreated &&
        (slot.phase == Phase::Ready || slot.phase == Phase::Stored)) {
        if (++next.nextSerial == 0)
            ++next.nextSerial;
        slot.ticket = {next.scene, next.nextSerial};
        slot.phase = Phase::Live;
        return result;
    }
    if (input.event == Event::UseSelected) {
        if (slot.phase == Phase::Ready) {
            if (++next.nextSerial == 0)
                ++next.nextSerial;
            slot.ticket = {next.scene, next.nextSerial};
            slot.phase = Phase::Creating;
            result.command = {Effect::Create, shape, slot.ticket};
        } else if (slot.phase == Phase::Live) {
            slot.phase = Phase::DetonationRequested;
            result.command = {Effect::Detonate, shape, slot.ticket};
        }
        return result;
    }
    if (slot.ticket != input.ticket || input.ticket.scene != next.scene)
        return result;
    switch (input.event) {
    case Event::Provisioned:
        if (slot.phase == Phase::Creating)
            slot.phase = Phase::Stored;
        break;
    case Event::CreationFailed:
        if (slot.phase == Phase::Creating)
            slot = {};
        break;
    case Event::DetonationFailed:
        if (slot.phase == Phase::DetonationRequested)
            slot.phase = Phase::Live;
        break;
    case Event::Exploded:
        if (slot.phase == Phase::Live || slot.phase == Phase::DetonationRequested) {
            slot.phase = Phase::Recharging;
            slot.rechargeSeconds = abilityProfile(next.upgrade).rechargeSeconds;
        }
        break;
    case Event::ActorLost:
        if (slot.phase == Phase::Live || slot.phase == Phase::DetonationRequested)
            slot = {};
        break;
    case Event::StockLost:
        // Stock used without an adoptable world bomb (e.g. on an arrow) recharges like a detonation.
        if (slot.phase == Phase::Stored) {
            slot.phase = Phase::Recharging;
            slot.rechargeSeconds = abilityProfile(next.upgrade).rechargeSeconds;
        }
        break;
    case Event::StockReplaced:
        // A save load replaced the whole pouch; nothing was used, so the stock returns at once.
        if (slot.phase == Phase::Stored) {
            slot.phase = Phase::Ready;
            slot.rechargeSeconds = 0.f;
        }
        break;
    default:
        break;
    }
    return result;
}
}
