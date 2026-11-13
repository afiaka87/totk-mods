// Copyright (c) HanSoloBlaster, afiaka87
// Adds Fieldworks' own content to the game files several mods replace whole, as the game loads them.
#pragma once

#include <cstdint>

namespace fieldworks::shared {

enum class Game { V121, V143 };

// Hooks the game's routine that reads and unpacks a file. Logs and installs nothing when the game's
// code there is not the code this build knows.
void install(std::uintptr_t mainBase, Game game);

}  // namespace fieldworks::shared
