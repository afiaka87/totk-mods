// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombReference.hpp"
#include <cstdint>
namespace remote_bomb::pickup {
enum class Stage { Idle, Waiting, Creating, Attention, Pressed, Refund };
struct Pending {
    Stage stage{};
    Shape shape{};
    float frames{};
    std::uint32_t epoch{};
    std::uint64_t inheritedButtons{};
    bool active() const { return stage != Stage::Idle; }
    bool advance(float delta) {
        if (active() && delta > 0) frames += delta;
        return frames >= 90;
    }
};
// Npad bits16..23 are stick directions, including the stick used to choose a wedge.
constexpr std::uint64_t actionButtons(std::uint64_t buttons) { return buttons & 0xffffu; }
inline bool cancelInput(Pending& pending, std::uint64_t buttons) {
    buttons = actionButtons(buttons);
    pending.inheritedButtons &= buttons;
    return (buttons & 2u) || (buttons & ~pending.inheritedButtons & ~(1u << 6));
}
// Each choice belongs to one wheel lifetime; stale choices must never create an item later.
constexpr std::uint64_t request(unsigned epoch, Shape shape) {
    return (std::uint64_t(epoch)<<32) | (shape==Shape::Cube ? 2u : 1u);
}
constexpr bool belongs(std::uint64_t request, unsigned epoch) {
    return request && epoch && unsigned(request>>32)==epoch;
}
constexpr bool intendedLift(std::uintptr_t intended, std::uintptr_t attentionActor, unsigned action) {
    return intended && intended==attentionActor && action==4;
}
}
