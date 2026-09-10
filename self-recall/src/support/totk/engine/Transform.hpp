
#pragma once

#include "totk/core/Result.hpp"
#include "totk/core/Units.hpp"
#include "totk/engine/ActorHandle.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Totk121Offsets.hpp"

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
    ComponentRegistryUnavailable,
    PhysicsComponentUnavailable,
    RigidBodySetUnavailable,
    RigidBodyUnavailable,
    EngineFunctionUnavailable,
};

using ForceSetMatrixFunction = void (*)(void*, const float*, std::uint32_t);
using GetMotionTypeFunction = std::uint32_t (*)(void*);
using RequestMotionTypeFunction = void (*)(void*, std::uint32_t);
using RequestLinearVelocityFunction = void (*)(void*, const float*);

struct TransformFunctions {
    ForceSetMatrixFunction forceSetMatrix = nullptr;
    GetMotionTypeFunction getMotionType = nullptr;
    RequestMotionTypeFunction requestMotionType = nullptr;
    RequestLinearVelocityFunction requestLinearVelocity = nullptr;

    [[nodiscard]] static TransformFunctions fromMainBase(std::uintptr_t mainBase) {
        if (!isPlausibleAddress(mainBase)) return {};
        return TransformFunctions{
            reinterpret_cast<ForceSetMatrixFunction>(
                mainBase + Totk121Offsets::kForceSetMatrix.value),
            reinterpret_cast<GetMotionTypeFunction>(
                mainBase + Totk121Offsets::kGetMotionType.value),
            reinterpret_cast<RequestMotionTypeFunction>(
                mainBase + Totk121Offsets::kRequestChangeMotionType.value),
            reinterpret_cast<RequestLinearVelocityFunction>(
                mainBase + Totk121Offsets::kRequestSetLinearVelocity.value),
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

    [[nodiscard]] core::Result<RigidBodyHandle, TransformError>
    rigidBody(const ActorHandle& actor, core::SceneToken scene) const {
        const auto error = validateHandle(actor, scene);
        if (error != TransformError::None) {
            return core::Result<RigidBodyHandle, TransformError>::failure(error);
        }

        const auto registry = readMemory<std::uintptr_t>(
            actor.address + layout::kActorComponentRegistry);
        if (!isPlausibleAddress(registry)) {
            return core::Result<RigidBodyHandle, TransformError>::failure(
                TransformError::ComponentRegistryUnavailable);
        }
        const auto physics = readMemory<std::uintptr_t>(
            registry + layout::kPhysicsFromRegistry);
        if (!isPlausibleAddress(physics)) {
            return core::Result<RigidBodyHandle, TransformError>::failure(
                TransformError::PhysicsComponentUnavailable);
        }
        const auto set = readMemory<std::uintptr_t>(
            physics + layout::kRigidBodySetFromPhysics);
        if (!isPlausibleAddress(set)) {
            return core::Result<RigidBodyHandle, TransformError>::failure(
                TransformError::RigidBodySetUnavailable);
        }
        const auto body =
            readMemory<std::uintptr_t>(set + layout::kRigidBodyFromSet);
        if (!isPlausibleAddress(body)) {
            return core::Result<RigidBodyHandle, TransformError>::failure(
                TransformError::RigidBodyUnavailable);
        }
        return core::Result<RigidBodyHandle, TransformError>::success(
            RigidBodyHandle{body, actor});
    }

    [[nodiscard]] core::Result<std::uint32_t, TransformError>
    motionType(const RigidBodyHandle& body, core::SceneToken scene) const {
        if (validateRigidBody(body, scene) != TransformError::None) {
            return core::Result<std::uint32_t, TransformError>::failure(
                TransformError::RigidBodyUnavailable);
        }
        if (!functions_.getMotionType) {
            return core::Result<std::uint32_t, TransformError>::failure(
                TransformError::EngineFunctionUnavailable);
        }
        return core::Result<std::uint32_t, TransformError>::success(
            functions_.getMotionType(reinterpret_cast<void*>(body.address)));
    }

    [[nodiscard]] TransformError requestMotionType(
        const RigidBodyHandle& body, core::SceneToken scene,
        std::uint32_t motionType) const {
        if (validateRigidBody(body, scene) != TransformError::None) {
            return TransformError::RigidBodyUnavailable;
        }
        if (!functions_.requestMotionType) {
            return TransformError::EngineFunctionUnavailable;
        }
        functions_.requestMotionType(reinterpret_cast<void*>(body.address), motionType);
        return TransformError::None;
    }

    [[nodiscard]] TransformError requestLinearVelocity(
        const RigidBodyHandle& body, core::SceneToken scene,
        core::LinearVelocity velocity) const {
        if (validateRigidBody(body, scene) != TransformError::None) {
            return TransformError::RigidBodyUnavailable;
        }
        if (!std::isfinite(velocity.x + velocity.y + velocity.z)) {
            return TransformError::NonFiniteTransform;
        }
        if (!functions_.requestLinearVelocity) {
            return TransformError::EngineFunctionUnavailable;
        }
        const float values[3] = {velocity.x, velocity.y, velocity.z};
        functions_.requestLinearVelocity(reinterpret_cast<void*>(body.address),
                                         values);
        return TransformError::None;
    }

private:
    [[nodiscard]] TransformError validateRigidBody(
        const RigidBodyHandle& body, core::SceneToken scene) const {
        if (!body.ownerIsCurrent(scene)) return TransformError::RigidBodyUnavailable;
        const auto current = rigidBody(body.owner, scene);
        if (!current || current.value.address != body.address) {
            return TransformError::RigidBodyUnavailable;
        }
        return TransformError::None;
    }

    TransformFunctions functions_{};
};

} // namespace totk::engine
