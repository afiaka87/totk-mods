// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "NativeActor.hpp"

namespace botw_abilities::wolf {

enum class PromptText : std::uint8_t { None, Stay, Come };
enum class LockOn : std::uint8_t { Nothing, Wolf, Other };

bool initializePromptApi(std::uintptr_t base);

// Shows the action prompt the way scripted game events do, which holds off Link's own prompts
// until hidePrompt, so every path that ends the wolf must hide it.
[[nodiscard]] bool showPrompt(PromptText text);
void hidePrompt();

[[nodiscard]] LockOn readLockOn(const ActorReference& player, std::uint32_t wolf);

}
