// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativeBombActions.hpp"
#include <limits>

namespace remote_bomb::engine {
namespace {
bool current(std::uint32_t scene, Actor actor) {
    return actor.value && actor.scene == scene;
}
struct ScopedReference {
    NativeReference value;
    void (*release)(NativeReference *);
    ~ScopedReference() { release(&value); }
};
void *memoryLink(void *lifter) {
    return static_cast<unsigned char *>(lifter) + 200;
}
}

Status requestDetonation(const Bindings &native, std::uint32_t scene, Actor bomb, DetonationRoute route) {
    if (!current(scene, bomb))
        return Status::StaleActor;
    if (route == DetonationRoute::DeathDrop) {
        if (!native.requestDeath)
            return Status::MissingBindings;
        // Chemical::update requests explosion death with reason 8 and subreason 0 in TotK 1.2.1.
        return native.requestDeath(bomb.value, 8, 0) ? Status::Accepted : Status::DeathRefused;
    }
    if (!native.getChemical || !native.detonate)
        return Status::MissingBindings;
    auto *chemical = native.getChemical(bomb.value);
    if (!chemical)
        return Status::MissingChemical;
    const Position inherited{std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f};
    native.detonate(chemical, 1, -1, &inherited);
    return Status::Accepted;
}

bool explosionReported(const Bindings &native, std::uint32_t scene, Actor bomb) {
    if (!current(scene, bomb) || !native.getChemical || !native.isExploded)
        return false;
    auto *chemical = native.getChemical(bomb.value);
    return chemical && native.isExploded(chemical, -1);
}
Actor carriedActor(const Bindings &native, std::uint32_t scene, Actor player) {
    if (!current(scene, player) || !native.getLifter || !native.heldReference ||
        !native.releaseReference)
        return {};
    auto *lifter = native.getLifter(player.value);
    if (!lifter)
        return {};
    ScopedReference reference{native.heldReference(memoryLink(lifter)), native.releaseReference};
    return {reference.value.value, scene};
}
bool discard(const Bindings &native, std::uint32_t scene, Actor actor) {
    return current(scene, actor) && native.deleteActor && native.deleteActor(actor.value, 0);
}
}
