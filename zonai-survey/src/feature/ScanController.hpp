// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cstdint>

#include "ScanVerdicts.hpp"

namespace zonai_survey::feature {

enum class ScanState : std::uint8_t {
    Idle,
    Pulsing,
    Holding,
};

class ScanController {
  public:
    void initialize(std::uintptr_t mainBase);

    pure::ScanVerdict trigger();

    void tick();

    [[nodiscard]] std::uint32_t pulseTicks() const { return tick_; }

    ScanState state() const { return state_; }

    float originX() const { return originX_; }
    float originY() const { return originY_; }
    float originZ() const { return originZ_; }
    std::uint32_t sceneGeneration() const { return scene_; }

    float headingX() const { return headingX_; }
    float headingZ() const { return headingZ_; }

  private:
    void abandon(const char* why);
    bool resolveLink(float& x, float& y, float& z, std::uint32_t& sceneGeneration);
    std::uintptr_t mainBase_ = 0;
    ScanState state_ = ScanState::Idle;
    std::uint32_t tick_ = 0;
    std::uint32_t scene_ = 0;
    float originX_ = 0.0f;
    float originY_ = 0.0f;
    float originZ_ = 0.0f;
    float headingX_ = 0.0f;
    float headingZ_ = 1.0f;
};

}
