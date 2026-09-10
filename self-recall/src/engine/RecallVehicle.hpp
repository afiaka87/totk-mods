#pragma once
#include <cstdint>

namespace self_recall::vehicle {
void install(std::uintptr_t mainBase);
void resetWorld();
bool controlStickActive(std::uintptr_t mainBase, const void* player);
bool controlStickRiding(std::uintptr_t mainBase, const void* player);
bool unmounted(const void* player);
bool detachControlStick(std::uintptr_t mainBase, void* player);
}
