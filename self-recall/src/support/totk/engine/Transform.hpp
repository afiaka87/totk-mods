
#pragma once

#include "totk/core/Types.hpp"
#include "totk/engine/Runtime.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace totk::engine {

enum class TransformError : std::uint8_t {
    None,
    HandleUnavailable,
    SceneChanged,
    IdentityChanged,
    NonFiniteTransform,
    EngineFunctionUnavailable,
};

using ForceSetMatrixFunction = void (*)(void*, const float*, std::uint32_t);

struct TransformFunctions {
    ForceSetMatrixFunction forceSetMatrix = nullptr;

    [[nodiscard]] static TransformFunctions fromMainBase(std::uintptr_t mainBase) {
        if (!isPlausibleAddress(mainBase)) return {};
        return {
            reinterpret_cast<ForceSetMatrixFunction>(
                mainBase + Totk121Offsets::kForceSetMatrix.value),
        };
    }
};

[[nodiscard]] inline TransformError validateHandle(const ActorHandle& handle,
                                                   core::SceneToken scene) {
    switch (handle.status(scene)) {
        case HandleStatus::Current: return TransformError::None;
        case HandleStatus::NullAddress: return TransformError::HandleUnavailable;
        case HandleStatus::SceneChanged: return TransformError::SceneChanged;
        case HandleStatus::IdentityChanged: return TransformError::IdentityChanged;
    }
    return TransformError::HandleUnavailable;
}

[[nodiscard]] inline bool isFinite(const core::WorldTransform& transform) {
    if (!std::isfinite(transform.position.x) ||
        !std::isfinite(transform.position.y) ||
        !std::isfinite(transform.position.z)) {
        return false;
    }
    for (float value : transform.rotation.values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

class TransformService {
public:
    explicit TransformService(TransformFunctions functions) : functions_(functions) {}

    [[nodiscard]] core::Result<core::WorldTransform, TransformError>
    read(const ActorHandle& actor, core::SceneToken scene) const {
        const auto error = validateHandle(actor, scene);
        if (error != TransformError::None) {
            return core::Result<core::WorldTransform, TransformError>::failure(error);
        }

        core::WorldTransform transform{};
        std::memcpy(&transform.position,
                    reinterpret_cast<const void*>(actor.address +
                                                  layout::kActorPosition),
                    sizeof(transform.position));
        std::memcpy(transform.rotation.values,
                    reinterpret_cast<const void*>(actor.address +
                                                  layout::kActorRotation),
                    sizeof(transform.rotation.values));
        if (!isFinite(transform)) {
            return core::Result<core::WorldTransform, TransformError>::failure(
                TransformError::NonFiniteTransform);
        }
        return core::Result<core::WorldTransform, TransformError>::success(transform);
    }

    [[nodiscard]] TransformError force(
        const ActorHandle& actor, core::SceneToken scene,
        const core::WorldTransform& transform,
        std::uint32_t engineMode = 0) const {
        const auto error = validateHandle(actor, scene);
        if (error != TransformError::None) return error;
        if (!isFinite(transform)) return TransformError::NonFiniteTransform;
        if (!functions_.forceSetMatrix) return TransformError::EngineFunctionUnavailable;

        const float matrix[12] = {
            transform.rotation.values[0], transform.rotation.values[1],
            transform.rotation.values[2], transform.position.x,
            transform.rotation.values[3], transform.rotation.values[4],
            transform.rotation.values[5], transform.position.y,
            transform.rotation.values[6], transform.rotation.values[7],
            transform.rotation.values[8], transform.position.z,
        };
        functions_.forceSetMatrix(reinterpret_cast<void*>(actor.address), matrix,
                                  engineMode);
        return TransformError::None;
    }

private:
    TransformFunctions functions_{};
};

}
