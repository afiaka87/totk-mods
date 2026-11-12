// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "NativeActor.hpp"
#include "totk/engine/Scene.hpp"

namespace botw_abilities::wolf {

// One input-thread owner; AI callbacks never access the creation handle or scene tracker.
class Lifecycle {
public:
    [[nodiscard]] ActorStatus update(const totk::engine::SceneSnapshot& scene);
    [[nodiscard]] bool create(const totk::core::WorldTransform& transform);
    void retire();
    [[nodiscard]] ActorReference borrow() { return actor_.borrow(); }
private:
    NativeActor actor_;
    totk::core::SceneToken scene_;
    totk::core::SceneGeneration sceneGeneration_;
    ActorStatus status_ = ActorStatus::Empty;
    bool ready_{};
};

}
