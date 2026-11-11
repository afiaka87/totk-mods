// Copyright (c) HanSoloBlaster, afiaka87
// Every hook a member installs goes through a gate: the member's code when on, the game's when off.
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

// Installs Phantom Foothold's climbing edits after every member; returns the count, zero when refused.
unsigned install(std::uintptr_t mainBase, const char* version);
void apply(bool on);

}  // namespace fieldworks::climb
