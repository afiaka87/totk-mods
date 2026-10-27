// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cmath>

namespace botw_abilities::champions {
enum class AppearancePhase { Idle, Loading, Playing, Removing };
enum class AppearanceAction { None, Spawn, Follow, Delete, Arrived, Removed, Timeout };
struct AppearanceState {
    AppearancePhase phase{};
    bool queued{};
    float waiting{};
    void request() { queued = true; }

    AppearanceAction step(bool alive, bool finished, float seconds) {
        const bool timed = phase == AppearancePhase::Loading || phase == AppearancePhase::Playing;
        if (timed && std::isfinite(seconds) && seconds > 0) waiting += seconds;
        if (alive) {
            if (phase == AppearancePhase::Removing) return AppearanceAction::None;
            if (phase == AppearancePhase::Loading) {
                phase = AppearancePhase::Playing;
                waiting = 0;
                return AppearanceAction::Arrived;
            }
            if (phase == AppearancePhase::Idle || finished || waiting > 10) {
                phase = AppearancePhase::Removing;
                return AppearanceAction::Delete;
            }
            return AppearanceAction::Follow;
        }
        if (phase == AppearancePhase::Playing || phase == AppearancePhase::Removing) {
            phase = AppearancePhase::Idle;
            return AppearanceAction::Removed;
        }
        if (phase == AppearancePhase::Loading) {
            if (waiting <= 5) return AppearanceAction::None;
            phase = AppearancePhase::Idle;
            queued = false;
            return AppearanceAction::Timeout;
        }
        if (!queued) return AppearanceAction::None;
        queued = false;
        waiting = 0;
        phase = AppearancePhase::Loading;
        return AppearanceAction::Spawn;
    }
};
}
