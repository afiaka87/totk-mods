
#pragma once

#include "totk/core/Result.hpp"
#include "totk/core/Units.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Totk121Offsets.hpp"

#include <cstdint>

namespace totk::engine {

enum class SceneResolveError : std::uint8_t {
    MainImageUnavailable,
    SceneModuleUnavailable,
    SceneUnavailable,
    ComponentsUnavailable,
    ResidentRosterUnavailable,
};

struct SceneContext {
    core::SceneToken token{};
    std::uintptr_t residentActorManager = 0;

    [[nodiscard]] bool isReady() const {
        return token.isValid() && isPlausibleAddress(residentActorManager);
    }
};

using SceneResult = core::Result<SceneContext, SceneResolveError>;

[[nodiscard]] inline SceneResult resolveSceneFromModuleSlot(std::uintptr_t sceneModuleSlot) {
    if (!isPlausibleAddress(sceneModuleSlot)) {
        return SceneResult::failure(SceneResolveError::MainImageUnavailable);
    }

    const auto sceneModule = readMemory<std::uintptr_t>(sceneModuleSlot);
    if (!isPlausibleAddress(sceneModule)) {
        return SceneResult::failure(SceneResolveError::SceneModuleUnavailable);
    }

    const auto scene =
        readMemory<std::uintptr_t>(sceneModule + layout::kSceneFromModule);
    if (!isPlausibleAddress(scene)) {
        return SceneResult::failure(SceneResolveError::SceneUnavailable);
    }

    const auto components =
        readMemory<std::uintptr_t>(scene + layout::kSceneComponents);
    if (!isPlausibleAddress(components)) {
        return SceneResult::failure(SceneResolveError::ComponentsUnavailable);
    }

    const auto manager = readMemory<std::uintptr_t>(
        components + sizeof(std::uintptr_t) * layout::kResidentActorComponentIndex);
    if (!isPlausibleAddress(manager)) {
        return SceneResult::failure(SceneResolveError::ResidentRosterUnavailable);
    }

    return SceneResult::success(SceneContext{core::SceneToken{scene}, manager});
}

[[nodiscard]] inline SceneResult resolveScene(std::uintptr_t mainBase) {
    if (!isPlausibleAddress(mainBase)) {
        return SceneResult::failure(SceneResolveError::MainImageUnavailable);
    }
    return resolveSceneFromModuleSlot(
        mainBase + Totk121Offsets::kSceneModuleInstance.value);
}

enum class SceneObservation : std::uint8_t {
    Unavailable,
    FirstScene,
    SameScene,
    ChangedScene,
};

struct SceneSnapshot {
    SceneContext context{};
    core::SceneGeneration generation{};
    SceneObservation observation = SceneObservation::Unavailable;
};

class SceneTracker {
public:
    [[nodiscard]] SceneSnapshot observe(const SceneResult& resolved) {
        if (!resolved) {
            current_ = {};
            return SceneSnapshot{{}, generation_, SceneObservation::Unavailable};
        }

        const bool hadScene = current_.token.isValid();
        const bool changed = hadScene && current_.token != resolved.value.token;
        if (!hadScene || changed) {
            ++generation_.value;
            if (generation_.value == 0) ++generation_.value;
        }
        current_ = resolved.value;
        return SceneSnapshot{
            current_,
            generation_,
            !hadScene ? SceneObservation::FirstScene
                      : changed ? SceneObservation::ChangedScene
                                : SceneObservation::SameScene,
        };
    }

    [[nodiscard]] SceneContext current() const { return current_; }
    [[nodiscard]] core::SceneGeneration generation() const { return generation_; }

private:
    SceneContext current_{};
    core::SceneGeneration generation_{};
};

} // namespace totk::engine
