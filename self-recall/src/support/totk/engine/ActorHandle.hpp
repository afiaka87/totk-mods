
#pragma once

#include "totk/core/Units.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Totk121Offsets.hpp"

#include <cstdint>

namespace totk::engine {

enum class HandleStatus : std::uint8_t {
    Current,
    NullAddress,
    SceneChanged,
    IdentityChanged,
};

struct ActorHandle {
    std::uintptr_t address = 0;
    std::uintptr_t capturedNamePointer = 0;
    core::SceneToken scene{};

    [[nodiscard]] HandleStatus status(core::SceneToken currentScene) const {
        if (!isPlausibleAddress(address)) return HandleStatus::NullAddress;
        if (scene != currentScene) return HandleStatus::SceneChanged;
        const auto currentName =
            readMemory<std::uintptr_t>(address + layout::kActorNamePointer);
        return currentName == capturedNamePointer ? HandleStatus::Current
                                                  : HandleStatus::IdentityChanged;
    }

    [[nodiscard]] bool isCurrent(core::SceneToken currentScene) const {
        return status(currentScene) == HandleStatus::Current;
    }
};

struct RigidBodyHandle {
    std::uintptr_t address = 0;
    ActorHandle owner{};

    [[nodiscard]] bool ownerIsCurrent(core::SceneToken currentScene) const {
        return isPlausibleAddress(address) && owner.isCurrent(currentScene);
    }
};

} // namespace totk::engine
