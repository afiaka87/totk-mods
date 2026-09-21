// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace arrowbound::pure {
struct MessageLiteral {
    const char16_t* text = nullptr;
    std::int32_t length = 0;
    std::uint32_t padding = 0;
    std::uint64_t attributes = UINT64_MAX;
};
static_assert(sizeof(MessageLiteral) == 24);
static_assert(offsetof(MessageLiteral, length) == 8);
static_assert(offsetof(MessageLiteral, attributes) == 16);

template <std::size_t N>
constexpr MessageLiteral literal(const char16_t (&text)[N]) {
    return {text, static_cast<std::int32_t>(N - 1), 0, UINT64_MAX};
}

inline constexpr auto kEmblemName = literal(u"Arrowbound Emblem");
inline constexpr auto kEmblemCaption = literal(
    u"Follow your arrows through the air.\nActivate to begin. Press B to let go.");
inline constexpr auto kActivate = literal(u"Activate");
inline constexpr auto kDeactivate = literal(u"Deactivate");

inline const MessageLiteral* emblemMessage(const char* table, const char* key) {
    if (!table || !key) return nullptr;
    if (std::strcmp(table, "ActorMsg/PouchContent") == 0) {
        if (std::strcmp(key, "Obj_CaveWellHonor_00_Name") == 0) return &kEmblemName;
        if (std::strcmp(key, "Obj_CaveWellHonor_00_Caption") == 0) return &kEmblemCaption;
    } else if (std::strcmp(table, "LayoutMsg/Pouch_00") == 0) {
        if (std::strcmp(key, "ArrowboundActivate") == 0) return &kActivate;
        if (std::strcmp(key, "ArrowboundDeactivate") == 0) return &kDeactivate;
    }
    return nullptr;
}
}
