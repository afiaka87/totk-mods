// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombWorld.hpp"
#include "BombIdentity.hpp"
#include <lib.hpp>

namespace remote_bomb::engine {
namespace eng = totk::engine;
const char *actorName(Shape shape, Upgrade upgrade) {
    return kActorNames[shape == Shape::Cube][upgrade == Upgrade::Plus];
}
bool Roster::contains(Handle actor) const {
    if (!actor.address)
        return false;
    for (unsigned i = 0; i < count; ++i)
        if (entries[i].actor.address == actor.address &&
            entries[i].actor.capturedNamePointer == actor.capturedNamePointer &&
            entries[i].actor.scene == actor.scene)
            return true;
    return false;
}
Roster scan(std::uintptr_t base, eng::SceneContext scene) {
    Roster result;
    const auto access = eng::resolveActiveProcessRoster(base, scene.token);
    if (!access) {
        Logging.Log("[remote-bomb] ROSTER_REFUSED error=%u", unsigned(access.error));
        return result;
    }
    bool overflow = false;
    const auto walk = eng::visitActiveProcesses(access.value, [&](const eng::ActiveProcessView &view) {
        for (unsigned form = 0; form < 2; ++form)
            for (unsigned upgrade = 0; upgrade < 2; ++upgrade)
                if (view.processName.equals(kActorNames[form][upgrade])
#ifdef BOTW_ABILITIES_NATIVE_SPHERE
                    || (!form && view.processName.equals(upgrade ? "Obj_RemoteBombSpherePlus_Attachment"
                                                                 : "Obj_RemoteBombSphere_Attachment"))
#endif
#ifdef BOTW_ABILITIES_NATIVE_CUBE
                    || (form && view.processName.equals(upgrade ? "Obj_RemoteBombCubePlus_Attachment"
                                                                : "Obj_RemoteBombCube_Attachment"))
#endif
                ) {
                    if (result.count == result.entries.size()) {
                        overflow = true;
                        return eng::VisitControl::Continue;
                    }
                    result.entries[result.count++] = {view.handle, form ? Shape::Cube : Shape::Sphere,
                                                       upgrade ? Upgrade::Plus : Upgrade::Base,
                                                       view.processState};
                }
        return eng::VisitControl::Continue;
    });
    result.complete = walk && walk.value.complete && !overflow;
    if (!result.complete)
        Logging.Log("[remote-bomb] ROSTER_INCOMPLETE walk_ok=%u visited=%u overflow=%u error=%u",
                    unsigned(bool(walk)), walk ? walk.value.visited : 0, unsigned(overflow),
                    walk ? 0 : unsigned(walk.error));
    return result;
}
Position position(Handle actor) {
    return eng::readMemory<Position>(actor.address + eng::layout::kActorPosition);
}
}
