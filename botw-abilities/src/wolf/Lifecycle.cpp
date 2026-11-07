// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Lifecycle.hpp"
#include "NativeBrain.hpp"
#include <lib.hpp>

namespace botw_abilities::wolf {

ActorStatus Lifecycle::update(const totk::engine::SceneSnapshot& scene) {
    ready_ = scene.context.isReady();
    if (!ready_ || scene_ != scene.context.token || sceneGeneration_ != scene.generation) {
        retire();
        scene_ = scene.context.token;
        sceneGeneration_ = scene.generation;
    }
    status_ = actor_.poll();
    selectBrainOwner(ready_ && status_ == ActorStatus::Ready ? actor_.generation() : UINT32_MAX);
    return status_;
}

bool Lifecycle::create(const totk::core::WorldTransform& transform) {
    if (!ready_ || (status_ != ActorStatus::Empty && status_ != ActorStatus::Failed)) {
        Logging.Log("[wolf-link] SUMMON_DEFERRED scene=%u state=%u", unsigned(ready_), unsigned(status_));
        return false;
    }
    if (!actor_.create(transform)) return false;
    status_ = ActorStatus::Creating;
    return true;
}

void Lifecycle::retire() {
    selectBrainOwner();
    actor_.retire();
    status_ = ActorStatus::Retiring;
}

}
