// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "StasisActors.hpp"
#include "StasisEnemyHold.hpp"
#include "totk/engine/ActorRoster.hpp"
#include <lib.hpp>
#include <cstring>
namespace stasis::engine {
namespace {
namespace eng = totk::engine;
using Getter = std::uintptr_t (*)(std::uintptr_t);
using VectorRequest = void (*)(std::uintptr_t, const Vec3 *);
std::uintptr_t equipmentUser(std::uintptr_t actor) {
    const auto equipment = call<Getter>(offsets::kGetDynamicEquipment)(actor);
    return valid(equipment) ? call<Getter>(offsets::kDynamicEquipmentGetUser)(equipment) : 0;
}
unsigned motionType(std::uintptr_t body) {
    return call<unsigned (*)(std::uintptr_t)>(offsets::kRigidBodyBaseGetMotionType)(body);
}
std::uintptr_t character(std::uintptr_t set) {
    const auto structure = physicsStructure(set);
    return valid(structure) ? read<std::uintptr_t>(structure + 304) : 0;
}
struct BodyVisitor {
    const std::uintptr_t *vtable;
    Bodies *output;
};
std::uint64_t visitBody(BodyVisitor *visitor, std::uintptr_t body, const void *) {
    auto &out = *visitor->output;
    if (!valid(body)) {
        out.overflow = true;
        return 1;
    }
    for (unsigned i = 0; i < out.count; ++i)
        if (out.address[i] == body)
            return 1;
    if (out.count == 32) {
        out.overflow = true;
        return 1;
    }
    out.address[out.count] = body;
    out.mode[out.count] = motionType(body);
    ++out.count;
    return 1;
}
// Nontrivial, so the game returns it through memory.
struct NativeGroup : target::Group {
    ~NativeGroup() {}
};
} // namespace

bool startsWith(const char *value, const char *prefix) {
    return value && std::strncmp(value, prefix, std::strlen(prefix)) == 0;
}
const char *actorName(std::uintptr_t actor) {
    if (!valid(actor))
        return nullptr;
    const auto name = read<std::uintptr_t>(actor + eng::layout::kActorNamePointer);
    return eng::isPlausibleStringAddress(name) ? reinterpret_cast<const char *>(name) : nullptr;
}
Vec3 position(std::uintptr_t actor) { return read<Vec3>(actor + eng::layout::kActorPosition); }
std::uintptr_t controllerSet(std::uintptr_t actor) {
    return call<Getter>(offsets::kActorBaseGetControllerSet)(actor);
}
std::uintptr_t physicsStructure(std::uintptr_t set) {
    return valid(set) ? read<std::uintptr_t>(set + 344) : 0;
}
std::uintptr_t mainBody(std::uintptr_t actor) {
    const auto set = controllerSet(actor);
    return valid(set) ? read<std::uintptr_t>(set + eng::layout::kRigidBodyFromSet) : 0;
}
float mass(std::uintptr_t body) {
    // The pending or cached mass, chosen the way the game's impulse call chooses it.
    const auto pending = read<std::uintptr_t>(body + 96);
    const bool requested = valid(pending) && (read<std::uint32_t>(pending + 212) & 0x10);
    return read<float>(requested ? pending + 104 : body + 304);
}
Bodies bodies(std::uintptr_t set) {
    Bodies out;
    static const std::uintptr_t vtable[]{reinterpret_cast<std::uintptr_t>(&visitBody)};
    BodyVisitor visitor{vtable, &out};
    if (valid(set))
        call<void (*)(std::uintptr_t, BodyVisitor *)>(offsets::kControllerSetVisitRigidBodies)(
            set, &visitor);
    return out;
}
bool worldBounds(std::uintptr_t actor, Bounds &out) {
    const auto body = mainBody(actor);
    if (!valid(body))
        return false;
    call<void (*)(std::uintptr_t, Bounds *)>(offsets::kRigidBodyBaseGetBoundingBoxWorld)(body,
                                                                                         &out);
    const Vec3 size = sub(out.max, out.min);
    return finite(out.min) && finite(out.max) && size.x > 0 && size.y > 0 && size.z > 0 &&
           length(size) < 100;
}
Vec3 centerOfMass(std::uintptr_t body) {
    Vec3 out{};
    call<void (*)(std::uintptr_t, Vec3 *)>(offsets::kRigidBodyGetCenterOfMass)(body, &out);
    return out;
}
std::uintptr_t animationController(std::uintptr_t actor) {
    const auto component = call<Getter>(offsets::kGetAnimationComponent)(actor);
    const auto wrapper = valid(component) ? read<std::uintptr_t>(component + 24) : 0;
    const auto animation = valid(wrapper) ? read<std::uintptr_t>(wrapper + 24) : 0;
    return valid(animation) ? animation : 0;
}
Weapon weaponOf(std::uintptr_t attacker) {
    auto weapon = attacker;
    ActorReference equipped;
    if (const char *name = actorName(weapon);
        startsWith(name, "Player") || startsWith(name, "Enemy_")) {
        const auto equipment = call<Getter>(offsets::kActorGetEquipment)(weapon);
        if (valid(equipment)) {
            // Keep the reference through classification; equipment slot zero starts at +32.
            auto reference = actorReference(equipment + 32);
            equipped.actor = reference.actor;
            equipped.owns = reference.owns;
            reference.owns = false;
            weapon = equipped.actor;
        }
    }
    Weapon out;
    const char *name = actorName(weapon);
    out.family = startsWith(name, "Weapon_Sword_")    ? 0
                 : startsWith(name, "Weapon_Lsword_") ? 1
                 : startsWith(name, "Weapon_Spear_")  ? 2
                                                      : -1;
    if (out.family >= 0)
        out.user = startsWith(actorName(attacker), "Player") ? attacker : equipmentUser(weapon);
    return out;
}
std::size_t snapshotRoster(totk::core::SceneToken scene, Handle *out, std::size_t capacity) {
    const auto access = eng::resolveActiveProcessRoster(moduleBase, scene);
    if (!access)
        return 0;
    const auto &roster = access.value;
    const auto mutex = roster.manager + eng::layout::kProcessListMutex;
    roster.lock(mutex);
    const int offset = read<int>(roster.manager + eng::layout::kProcessActiveLinkOffset);
    if (offset <= 0 || offset >= 4096) {
        roster.unlock(mutex);
        return 0;
    }
    std::size_t count = 0;
    const auto anchor = roster.manager + eng::layout::kProcessActiveHead - 8;
    auto node = read<std::uintptr_t>(anchor + 8);
    for (unsigned walked = 0; node != anchor && valid(node) && walked < 4096; ++walked) {
        const auto actor = node - static_cast<std::uintptr_t>(offset);
        const auto name = read<std::uintptr_t>(actor + eng::layout::kActorNamePointer);
        if (eng::isPlausibleStringAddress(name) && count < capacity)
            out[count++] = {actor, name, scene};
        node = read<std::uintptr_t>(node + 8);
    }
    roster.unlock(mutex);
    return count;
}
bool eligible(Handle handle) {
    const char *name = reinterpret_cast<const char *>(handle.capturedNamePointer);
    for (const char *prefix :
         {"Player", "Camera", "GameCamera", "Npc_", "NPC_", "Effect_", "Event", "Area_", "Dm_",
          "Enemy_Dragon", "Enemy_Ganon", "Enemy_Giant", "Enemy_Boss"})
        if (startsWith(name, prefix))
            return false;
    // Carried or fused gear has an owner even while its body is dynamic: Link's sword, bow, shield.
    if (equipmentUser(handle.address))
        return false;
    const auto body = mainBody(handle.address);
    if (!valid(body))
        return false;
    const float scalar = mass(body);
    if (!std::isfinite(scalar) || scalar <= 0.0f)
        return false;
    const auto mode = motionType(body);
    if (startsWith(name, "Enemy_") && valid(character(controllerSet(handle.address))))
        return true;
    return mode == kDynamic;
}
bool holdAvailable(std::uintptr_t structure) {
    const int count = read<int>(structure + 208);
    const auto entities = read<std::uintptr_t>(structure + 216);
    if (count < 0 || count > 256 || (count && !valid(entities))) {
        Logging.Log("[stasis] ACTIVATE_REFUSED entity_count=%d entities=%p", count,
                    reinterpret_cast<void *>(entities));
        return false;
    }
    for (int i = 0; i < count; ++i) {
        const auto wrapper = read<std::uintptr_t>(entities + sizeof(std::uintptr_t) * i);
        const auto entity = valid(wrapper) ? read<std::uintptr_t>(wrapper) : 0;
        if (!valid(entity) || (read<std::uint32_t>(entity + 16) & 0x20000)) {
            Logging.Log("[stasis] ACTIVATE_REFUSED existing hold or invalid entity index=%d", i);
            return false;
        }
    }
    return true;
}
bool readPose(std::uintptr_t actor, float (&matrix)[12]) {
    const auto p = position(actor);
    const auto *rotation = reinterpret_cast<const float *>(actor + eng::layout::kActorRotation);
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 3; ++col)
            matrix[row * 4 + col] = rotation[row * 3 + col];
    matrix[3] = p.x;
    matrix[7] = p.y;
    matrix[11] = p.z;
    for (float value : matrix)
        if (!std::isfinite(value))
            return false;
    return true;
}
target::Group collisionGroup(std::uintptr_t actor) {
    return call<NativeGroup (*)(const std::uintptr_t *)>(offsets::kActorReferenceGetSystemGroupID)(
        &actor);
}
void resolveHitBody(std::uintptr_t body, target::Hit &hit) {
    const auto link = call<Getter>(offsets::kRigidBodyEntityGetActorLink)(body);
    if (!valid(link))
        return;
    const auto reference = actorReference(link);
    if (const auto *name = actorName(reference.actor)) {
        hit.actor = reference.actor;
        hit.name = reinterpret_cast<std::uintptr_t>(name);
    }
}

void freeze(std::uintptr_t set) {
    call<void (*)(std::uintptr_t)>(offsets::kControllerSetSetFreeze)(set);
}
void unfreeze(std::uintptr_t set) {
    call<void (*)(std::uintptr_t)>(offsets::kControllerSetSetUnfreeze)(set);
}
void setMotionType(std::uintptr_t body, unsigned mode) {
    call<void (*)(std::uintptr_t, int)>(offsets::kRigidBodyBaseRequestChangeMotionType)(body, mode);
}
void stopBody(std::uintptr_t body) {
    const Vec3 zero{};
    call<VectorRequest>(offsets::kRigidBodyBaseRequestSetLinearVelocity)(body, &zero);
    call<VectorRequest>(offsets::kRigidBodyBaseRequestSetAngularVelocity)(body, &zero);
}
void applyImpulse(std::uintptr_t body, Vec3 impulse) {
    call<VectorRequest>(offsets::kRigidBodyApplyLinearImpulse)(body, &impulse);
}
void forcePose(std::uintptr_t actor, const float *pose) {
    call<void (*)(std::uintptr_t, const float *, unsigned)>(offsets::kActorBaseForceSetMatrix)(
        actor, pose, 0);
}
void commitPose(std::uintptr_t actor, const float *pose, float frameScale) {
    call<float (*)(std::uintptr_t, const float *, float)>(offsets::kActorSetPose)(actor, pose,
                                                                                  frameScale);
    // A correction must not become next frame's movement.
    const float zero[kActorMotionFloats]{};
    std::memcpy(reinterpret_cast<void *>(actor + kActorMotionOffset), zero, sizeof(zero));
}
} // namespace stasis::engine
