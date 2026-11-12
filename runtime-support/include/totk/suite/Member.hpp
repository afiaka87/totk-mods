// Copyright (c) HanSoloBlaster, afiaka87
// The contract between a standalone mod and Zonai Fieldworks (design 06). A mod's standalone exl_main
// and Fieldworks call the same install function; only Fieldworks reads the rest of this header.
#pragma once

#include <cstdint>

namespace totk::suite {

struct Member {
    const char* id;                   // settings key, "phantom-foothold"
    const char16_t* caption;          // wheel caption, u"Phantom Foothold"
    bool (*install)(std::uintptr_t);  // installs hooks; false = this game version is unsupported
    // Optional, on the controller thread: on starts clean; off ends, cleans up, and hooks run on through the stop time.
    void (*enabled)(bool on) = nullptr;
    // Optional. While it returns true the member is doing something it cannot end cleanly (riding a linked
    // vehicle, a camp warp), and a switch to off waits until it returns false.
    bool (*busy)() = nullptr;
};

// Hooks installed while an AlwaysOn is alive stay on regardless of the member's switch (early, stateful, wheel).
inline int alwaysOnDepth = 0;

struct AlwaysOn {
    AlwaysOn() { ++alwaysOnDepth; }
    ~AlwaysOn() { --alwaysOnDepth; }
    AlwaysOn(const AlwaysOn&) = delete;
    AlwaysOn& operator=(const AlwaysOn&) = delete;
};

// Defined by Fieldworks. Members that use fixed 1.2.1 addresses refuse other versions with it.
bool gameIs121();

// Defined by Fieldworks: the code word before any member hooked it, so other members' hooks don't fail checks.
std::uint32_t originalCode(std::uintptr_t address);

// The word a member's code check compares: the game's own word in Fieldworks, the live word otherwise.
inline std::uint32_t checkedCode(std::uintptr_t address) {
#ifdef TOTK_SUITE_MEMBER
    return originalCode(address);
#else
    return *reinterpret_cast<const volatile std::uint32_t*>(address);
#endif
}

}  // namespace totk::suite

#define TOTK_SUITE_CONCAT_INNER(a, b) a##b
#define TOTK_SUITE_CONCAT(a, b) TOTK_SUITE_CONCAT_INNER(a, b)
#ifdef TOTK_SUITE_MEMBER
#define TOTK_SUITE_ALWAYS_ON() const ::totk::suite::AlwaysOn TOTK_SUITE_CONCAT(totkSuiteAlwaysOn, __LINE__)
#else
#define TOTK_SUITE_ALWAYS_ON() static_cast<void>(0)
#endif
