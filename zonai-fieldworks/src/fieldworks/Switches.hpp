// Copyright (c) HanSoloBlaster, afiaka87
// The player's on/off switch for each member, kept on the SD card and shared by every save.
#pragma once

#include <cstddef>
#include <cstdint>

#include "totk/suite/Member.hpp"
#include "wheel/WheelPage.hpp"

namespace fieldworks::switches {

inline constexpr char kFile[] = "sdcard:/ZonaiFieldworks/settings.txt";

void configure(const totk::suite::Member* members, const char* const* icons, const bool* supported,
               std::size_t count);

// Called on every controller update, before the members' own controller hooks.
void tick();

// True while the member's hooks run its code, including its stop time (false until the file is read).
bool active(std::size_t member);

const botw_abilities::wheel::ExtraPage* wheelPage();

}  // namespace fieldworks::switches
