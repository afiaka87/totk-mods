// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <atomic>
#include <cstdint>

namespace zonai_survey::engine::perf {


inline std::uint64_t now() { return 0; }
inline std::uint64_t frequency() { return 1; }

struct Bucket {
    void add(std::uint64_t) {}
    void reset() {}
};

class Timer {
  public:
    explicit Timer(Bucket&) {}
};

inline Bucket gameTick, gameScan, gameRebuild, gameGlyphs, gameStatus;
inline std::uint32_t gameStatusChanges = 0;
inline std::atomic<std::uint32_t> workerSeen{0};
inline std::atomic<std::uint64_t> workerSum{0};
inline std::atomic<std::uint32_t> workerCalls{0};
inline void addWorkerSample(std::uint64_t) {}
inline Bucket drawSurveyTotal, drawVis, drawFillOutline, drawFillCore,
    drawGlyphText;
inline std::uint32_t drawLayerCalls = 0, drawGameplayCalls = 0,
    drawSkipNoFrame = 0, drawSkipHidden = 0, drawSkipEmpty = 0;


}
