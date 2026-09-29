// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#include <lib.hpp>

#include "ZonaiSurveyModule.hpp"
#include "ZonaiSurveyIntegration.hpp"

#include "GlyphController.hpp"
#include "AtlasRenderer.hpp"
#include "ScanController.hpp"
#include "ScanPresentation.hpp"
#include "SurveyOptions.hpp"
#include "Audio.hpp"
#include "totk/engine/Npad.hpp"

namespace {

constexpr std::uint64_t BTN_ZL = 1ull << 8;
constexpr std::uint64_t BTN_UP = 1ull << 13;

zonai_survey::feature::ScanController g_scan{};
zonai_survey::feature::GlyphController g_glyphs{};
totk::engine::NpadReader g_npad{};
std::uint64_t g_previousButtons = 0;
std::uint64_t g_ownedSurveyButton = 0;
bool g_ready = false;
zonai_survey::pure::SurveyCooldown g_cooldown{}, g_refusalSound{};
zonai_survey::feature::ScanState g_previousState = zonai_survey::feature::ScanState::Idle;

void moduleEnter() {
    g_previousButtons = 0;
    g_ownedSurveyButton = 0;
    g_previousState = zonai_survey::feature::ScanState::Idle;
}

void moduleInit(std::uintptr_t mainBase) {
    g_cooldown = {}; g_refusalSound = {};
    g_scan.initialize(mainBase);
    g_glyphs.initialize(mainBase);
    moduleEnter();
    g_ready = true;
}

void serviceSurveyInput(const totk::engine::NpadFrame& frame, std::uint64_t buttons,
                        std::uint64_t pressed) {
    if ((buttons & BTN_ZL) != 0) g_ownedSurveyButton |= buttons & BTN_UP;
    frame.maskOwnedButtons(g_ownedSurveyButton);

    if ((buttons & BTN_ZL) == 0 || (pressed & BTN_UP) == 0) return;
    const auto verdict = zonai_survey::integration::triggerSurvey();
    if (verdict == zonai_survey::pure::ScanVerdict::CoolingDown) return;
    if (verdict != zonai_survey::pure::ScanVerdict::Accepted)
        zonai_survey::render::atlasBanner("Cannot survey here",
                                          zonai_survey::presentation::displayText(verdict), 150);
}

void serviceSurveyCompletion() {
    g_scan.tick();
    g_glyphs.tick();
    const auto state = g_scan.state();
    if (state == zonai_survey::feature::ScanState::Idle && g_previousState != state)
        g_glyphs.clear();
    g_previousState = state;
}

void moduleTick(void* npadDevice) {
    if (!g_ready) return;

    if (npadDevice) {
        const auto frame = g_npad.read(npadDevice);
        const std::uint64_t buttons = frame.snapshot().buttons;
        const std::uint64_t pressed = buttons & ~g_previousButtons;
        g_previousButtons = buttons;
        g_ownedSurveyButton &= buttons;
        serviceSurveyInput(frame, buttons, pressed);
    }
    serviceSurveyCompletion();
}

bool moduleRequestExit() { return true; }

constexpr wwpg::Module kModule{
     "Zonai Survey",
     nullptr,
     nullptr,
     nullptr,
     &moduleInit,
     &moduleEnter,
     &moduleTick,
     &moduleRequestExit,
     nullptr,
     nullptr,
     nullptr,
};

}

namespace zonai_survey::integration {

pure::ScanVerdict triggerSurvey() {
    const auto now = svcGetSystemTick();
    if (g_cooldown.remaining(now)) {
        if (!g_refusalSound.remaining(now)) {
            audio::playCue(pure::kSurveyRefusalCue);
            g_refusalSound.started = now;
            g_refusalSound.duration = pure::kSystemTicksPerSecond / 4;
        }
        return pure::ScanVerdict::CoolingDown;
    }
    const pure::ScanVerdict verdict = g_scan.trigger();
    if (verdict != pure::ScanVerdict::Accepted) return verdict;
    g_cooldown.start(svcGetSystemTick(), options::cooldownSeconds());
    g_glyphs.onPulse(g_scan.originX(), g_scan.originY(), g_scan.originZ(),
                     g_scan.headingX(), g_scan.headingZ(), g_scan.sceneGeneration());
    return verdict;
}

}

namespace wwpg::modules {
const Module& zonaiSurvey() { return kModule; }
}
