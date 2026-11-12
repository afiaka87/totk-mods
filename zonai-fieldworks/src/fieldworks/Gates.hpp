// Copyright (c) HanSoloBlaster, afiaka87
// Fieldworks installs every member at load; each hook goes through a gate to the member's or the game's code.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fieldworks::gates {

inline constexpr std::size_t kMembers = 8;

struct Counts {
    unsigned gated;     // switched with the member
    unsigned alwaysOn;  // installed under totk::suite::AlwaysOn
};

// Hooks installed between these calls belong to the member. Outside them hooks stay on.
void beginMember(std::size_t member);
void endMember();

// False when a member ran out of gate or hook capacity; its gates then stay off.
bool fits(std::size_t member);
Counts counts(std::size_t member);
unsigned stubsUsed();

// How exlaunch wrote the hooks: one branch (near) or a longer sequence (far), and how many hook sites
// keep the game's original words for the members' code checks (totk::suite::originalCode).
struct HookForms {
    unsigned nearHooks;
    unsigned farHooks;
    unsigned recordedSites;
};
HookForms hookForms();

// Routes every gate of the member to its code (on) or to the game's original code (off).
void apply(std::size_t member, bool on);

}  // namespace fieldworks::gates

namespace fieldworks::climb {

// Climbing edits via switchable stubs; install after all members (a changed site refuses), returns edit count.
unsigned install(std::uintptr_t mainBase, const char* version);
void apply(bool on);

}  // namespace fieldworks::climb
