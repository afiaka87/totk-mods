// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
namespace sead { class Heap; }
namespace zonai_survey::engine {
inline std::atomic<bool> g_atlasInputReady{!SURVEY_STARTUP_DIAGNOSTIC};
inline sead::Heap* g_atlasObservedHeap{};
}
