// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ActorRuntime.hpp"

#include <cstddef>

namespace bivouac::engine {
namespace {

constexpr std::ptrdiff_t kActorNameOffset = 0x218;
constexpr std::ptrdiff_t kActorComponentRegistryOffset = 0x228;
constexpr std::ptrdiff_t kPhysicsComponentOffset = 0x050;
constexpr std::ptrdiff_t kControllerSetOffset = 0x020;
constexpr std::ptrdiff_t kMainRigidBodyOffset = 0x150;
constexpr std::ptrdiff_t kPreactorActorOffset = 0x20;

constexpr std::ptrdiff_t kActorManagerInstanceOffset = 0x04722920;
constexpr std::ptrdiff_t kRequestCreateActorAsyncOffset = 0x00AB92CC;
constexpr std::ptrdiff_t kForceSetMatrixOffset = 0x006BA86C;
constexpr std::ptrdiff_t kRequestChangeMotionTypeOffset = 0x00E8BE30;
constexpr std::ptrdiff_t kRequestSetLinearVelocityOffset = 0x00ACC874;
constexpr std::ptrdiff_t kRequestDeleteOffset = 0x0083AA68;
constexpr std::ptrdiff_t kPhiveSystemGlobalOffset = 0x0462E038;

struct NativeCreateArgument {
    float position[3];
    float rotation[9];
    float scale[3];
    const char* nameSlot;
    std::uint8_t opaque48[0x10];
    void* blackboardInfo;
    void* opaque60;
    void* parent;
    void* dependent;
    void* creator;
    void* creatorRtti;
    void* instanceHeap;
    std::uint8_t transformFlags;
    std::uint8_t opaque91[7];
};
static_assert(sizeof(NativeCreateArgument) == 0x98);
static_assert(offsetof(NativeCreateArgument, nameSlot) == 0x40);
static_assert(offsetof(NativeCreateArgument, blackboardInfo) == 0x58);
static_assert(offsetof(NativeCreateArgument, transformFlags) == 0x90);

using RequestCreateActorAsync = bool (*)(
    void*, const void*, const NativeCreateArgument*, void*, std::uint32_t,
    void*, void*, void*, bool, std::uint32_t*, void**);

bool validPointer(std::uintptr_t value) {
    return value >= 0x1000000 && value < 0x8000000000ull
        && (value & 0x3) == 0;
}

} // namespace

SpawnResult ActorRuntime::requestSpawn(
    const char* actorName, const SpawnTransform& transform) const {
    if (mMainBase == 0 || actorName == nullptr) {
        return {};
    }

    void* actorManager =
        *reinterpret_cast<void**>(mMainBase + kActorManagerInstanceOffset);
    if (actorManager == nullptr) {
        return {};
    }

    NativeCreateArgument argument{};
    for (int index = 0; index < 3; ++index) {
        argument.position[index] = transform.position[index];
        argument.scale[index] = transform.scale[index];
    }
    for (int index = 0; index < 9; ++index) {
        argument.rotation[index] = transform.rotation[index];
    }
    static const char* kEmptyName = "";
    argument.nameSlot = kEmptyName;
    argument.blackboardInfo = nullptr;
    argument.transformFlags = 0x7;

    std::uint32_t nativeResult = 0;
    void* preactor = nullptr;
    const auto requestCreate = reinterpret_cast<RequestCreateActorAsync>(
        mMainBase + kRequestCreateActorAsyncOffset);
    const bool accepted = requestCreate(
        actorManager, &actorName, &argument, nullptr,
        /* CreatePriority::High */ 1, nullptr, nullptr, nullptr, false,
        &nativeResult, &preactor);
    return {accepted, nativeResult, preactor};
}

const char* ActorRuntime::actorName(const void* actor) const {
    if (actor == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<const char* const*>(
        reinterpret_cast<std::uintptr_t>(actor) + kActorNameOffset);
}

void* ActorRuntime::actorFromPreactor(const void* preactor) const {
    if (preactor == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<void* const*>(
        reinterpret_cast<std::uintptr_t>(preactor) + kPreactorActorOffset);
}

void* ActorRuntime::mainRigidBody(void* actor) const {
    if (actor == nullptr) {
        return nullptr;
    }
    const auto registry = *reinterpret_cast<std::uintptr_t*>(
        reinterpret_cast<std::uintptr_t>(actor) + kActorComponentRegistryOffset);
    if (!validPointer(registry)) {
        return nullptr;
    }
    const auto physics =
        *reinterpret_cast<std::uintptr_t*>(registry + kPhysicsComponentOffset);
    if (!validPointer(physics)) {
        return nullptr;
    }
    const auto controllerSet =
        *reinterpret_cast<std::uintptr_t*>(physics + kControllerSetOffset);
    if (!validPointer(controllerSet)) {
        return nullptr;
    }
    const auto body =
        *reinterpret_cast<std::uintptr_t*>(controllerSet + kMainRigidBodyOffset);
    return validPointer(body) ? reinterpret_cast<void*>(body) : nullptr;
}

void ActorRuntime::forceSetMatrix(void* actor, const float rows[12]) const {
    const auto function = reinterpret_cast<void (*)(void*, const float*, std::uint32_t)>(
        mMainBase + kForceSetMatrixOffset);
    function(actor, rows, 0);
}

void ActorRuntime::requestMotionType(void* body, std::uint32_t type) const {
    const auto function = reinterpret_cast<void (*)(void*, std::uint32_t)>(
        mMainBase + kRequestChangeMotionTypeOffset);
    function(body, type);
}

void ActorRuntime::zeroLinearVelocity(void* body) const {
    const float velocity[3] = {};
    const auto function = reinterpret_cast<void (*)(void*, const float*)>(
        mMainBase + kRequestSetLinearVelocityOffset);
    function(body, velocity);
}

void ActorRuntime::requestDelete(void* actor) const {
    const auto function = reinterpret_cast<void (*)(void*)>(
        mMainBase + kRequestDeleteOffset);
    function(actor);
}

std::uint64_t ActorRuntime::hitOwner(std::uint32_t bodyId) const {
    if (bodyId == 0xFFFFFFFFu) {
        return kOwnerUnresolved;
    }

    const auto moduleEnd = mMainBase + 0x5000000;
    const auto global =
        *reinterpret_cast<std::uint64_t*>(mMainBase + kPhiveSystemGlobalOffset);
    if (!validPointer(global)) {
        return kOwnerUnresolved;
    }
    const auto system = *reinterpret_cast<std::uint64_t*>(global);
    if (!validPointer(system)) {
        return kOwnerUnresolved;
    }
    const auto worldManager = *reinterpret_cast<std::uint64_t*>(system + 0xC8);
    if (!validPointer(worldManager)) {
        return kOwnerUnresolved;
    }
    const auto vtable = *reinterpret_cast<std::uint64_t*>(worldManager);
    if (vtable < mMainBase || vtable >= moduleEnd) {
        return kOwnerUnresolved;
    }
    const auto getWorld = *reinterpret_cast<std::uint64_t*>(vtable + 0x60);
    if (getWorld < mMainBase || getWorld >= moduleEnd) {
        return kOwnerUnresolved;
    }
    const auto world = reinterpret_cast<std::uint64_t (*)(
        std::uint64_t, std::uint32_t)>(getWorld)(worldManager, 0);
    if (!validPointer(world)) {
        return kOwnerUnresolved;
    }
    const auto hknpWorld = *reinterpret_cast<std::uint64_t*>(world + 0xE0);
    if (!validPointer(hknpWorld)) {
        return kOwnerUnresolved;
    }
    const auto bodyArray = *reinterpret_cast<std::uint64_t*>(hknpWorld + 0x38);
    if (!validPointer(bodyArray)) {
        return kOwnerUnresolved;
    }
    const auto body = bodyArray + 192ull * (bodyId & 0xFFFFFF);
    if (!validPointer(body)) {
        return kOwnerUnresolved;
    }
    const auto rigidBody = *reinterpret_cast<std::uint64_t*>(body + 0xB8);
    if (!validPointer(rigidBody)) {
        return kOwnerUnresolved;
    }
    const auto owner = *reinterpret_cast<std::uint64_t*>(rigidBody + 0x110);
    if (owner == 0) {
        return 0;
    }
    return validPointer(owner) ? owner : kOwnerUnresolved;
}

} // namespace bivouac::engine
