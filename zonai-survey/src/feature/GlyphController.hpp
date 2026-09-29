// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cstdint>

#include "GlyphLife.hpp"
#include "SurveySweep.hpp"
#include "SurveyOptions.hpp"
#include "totk/engine/ActorHandle.hpp"

namespace zonai_survey::feature {

class GlyphController {
  public:
    void initialize(std::uintptr_t mainBase);

    void onPulse(float originX, float originY, float originZ, float headingX,
                 float headingZ, std::uint32_t sceneGeneration);

    void tick();
    void clear();

  private:
#if SURVEY_CONSTRAINED
    float range_{options::nextRange()};
    float scanRange() const { return range_; }
#else
    static constexpr float scanRange() { return pure::kMaxRange; }
#endif
    void forget();
    std::uint32_t revealTick(const pure::Glyph& glyph) const;
    void gatherFromMap();
    void rebuildRosterCandidates();
    void refreshLivePositions();
    void publish();

    std::uintptr_t mainBase_ = 0;
    std::uint32_t scene_ = 0;
    std::uint32_t tick_ = 0;
    std::uint32_t pulseTick_ = 0;
    bool active_ = false;

    float originX_ = 0.0f, originY_ = 0.0f, originZ_ = 0.0f;
    float headingX_ = 0.0f, headingZ_ = 1.0f;

    struct MapMark {
        pure::Glyph glyph{};
        std::uint32_t revealTick = 0;
    };
    MapMark map_[pure::kMaxGlyphs]{};
    std::uint32_t mapCount_ = 0;

    static constexpr std::uint32_t kMaxLiveCandidates = 96;
    struct LiveCandidate {
        totk::engine::ActorHandle handle{};
        std::uint16_t name = 0;
        std::uint8_t cls = 0;
        bool alive = false;
    };
    LiveCandidate candidates_[kMaxLiveCandidates]{};
    std::uint32_t candidateCount_ = 0;
    std::uint32_t lastRosterTick_ = 0;

    pure::Glyph live_[pure::kMaxGlyphs]{};
    std::uint32_t liveCount_ = 0;
};

}
