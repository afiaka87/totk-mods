// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "totk/engine/GameDataQueue.hpp"

namespace totk::engine {

inline constexpr std::uint32_t kKeyItemCategory = 8;
inline constexpr int kKeyItemGrantPushes = 6;

using ResolvePouchCategory = std::uint8_t (*)(std::uint32_t*, const char**);
using AddToPouch = std::uint64_t (*)(
    void*, const char**, void*, std::uint32_t, std::int32_t, std::uint8_t,
    std::int32_t, std::uint8_t, std::uint32_t, std::uint32_t, std::uint32_t,
    std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t*, std::uint8_t);

struct KeyItemGrantFunctions {
    ResolvePouchCategory resolveCategory = nullptr;
    AddToPouch addToPouch = nullptr;
};

enum class KeyItemGrantError : std::uint8_t {
    None, GameplayNotReady, ManagerUnavailable, QueueUnavailable,
    FunctionUnavailable, WrongCategory, NativeRefusal,
};

struct KeyItemGrantResult {
    KeyItemGrantError error = KeyItemGrantError::None;
    std::uint32_t category = 0;
    std::uint64_t nativeResult = 0;

    explicit operator bool() const { return error == KeyItemGrantError::None; }
};

inline KeyItemGrantResult grantKeyItem(
    std::uintptr_t manager, std::uintptr_t pouch, const char* actor, void* const* modifierSlot,
    KeyItemGrantFunctions functions, GameDataWriteReadiness readiness) {
    if (readiness != GameDataWriteReadiness::LiveGameplayAndWorkerAlive)
        return {KeyItemGrantError::GameplayNotReady};
    if (!isPlausibleAddress(manager) || !isPlausibleAddress(pouch))
        return {KeyItemGrantError::ManagerUnavailable};
    if (!inspectGameDataQueue(manager + 200, kKeyItemGrantPushes) ||
        !inspectGameDataQueue(manager + 296, kKeyItemGrantPushes))
        return {KeyItemGrantError::QueueUnavailable};
    if (!actor || !modifierSlot || !functions.resolveCategory || !functions.addToPouch)
        return {KeyItemGrantError::FunctionUnavailable};

    struct NameRef {
        const char* cstr;
        std::uint64_t pad[3];
    } nameRef{actor, {0, 0, 0}};
    auto* name = reinterpret_cast<const char**>(&nameRef);
    std::uint32_t category = 0;
    if (!(functions.resolveCategory(&category, name) & 1u) || category != kKeyItemCategory)
        return {KeyItemGrantError::WrongCategory, category};

    const auto result = functions.addToPouch(
        reinterpret_cast<void*>(pouch), name, *modifierSlot, category, 1, 1, 0,
        0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, nullptr, 1);
    return {result ? KeyItemGrantError::None : KeyItemGrantError::NativeRefusal, category, result};
}

} // namespace totk::engine
