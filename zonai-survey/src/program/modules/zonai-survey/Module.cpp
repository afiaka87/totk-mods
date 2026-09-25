// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#include <lib.hpp>

#include "ZonaiSurveyModule.hpp"
#include "ZonaiSurveyIntegration.hpp"

#include "GlyphController.hpp"
#include "GlyphRenderer.hpp"
#include "AtlasRenderer.hpp"
#include "PerfCounters.hpp"
#include "PerfReport.hpp"
#include "ScanBatching.hpp"
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

char g_reachText[64]{};

void appendText(char*& out, const char* end, const char* text) {
    while (text && *text != '\0' && out < end) *out++ = *text++;
}

void appendUInt(char*& out, const char* end, std::uint32_t value) {
    char digits[12]{};
    int count = 0;
    if (value == 0) digits[count++] = '0';
    while (value > 0 && count < 12) {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    while (count > 0 && out < end) *out++ = digits[--count];
}

void buildReachText(const zonai_survey::feature::ScanDiagnostics& diagnostics) {
    char* out = g_reachText;
    const char* const end = g_reachText + sizeof(g_reachText) - 1;
    appendUInt(out, end, diagnostics.reachMeters);
    appendText(out, end, " m of ground (");
    appendUInt(out, end, diagnostics.reachRing);
    appendText(out, end, " of ");
    appendUInt(out, end, zonai_survey::pure::kRings);
    appendText(out, end, " rings)");
    *out = '\0';
}

void moduleInit(std::uintptr_t mainBase) {
    g_cooldown = {}; g_refusalSound = {};
    g_scan.initialize(mainBase);
    g_glyphs.initialize(mainBase);
    g_previousButtons = 0;
    g_ownedSurveyButton = 0;
    g_previousState = zonai_survey::feature::ScanState::Idle;
    g_reachText[0] = '\0';
    g_ready = true;
    Logging.Log("[zonai-survey] module init");
}

void moduleEnter() {
    g_previousButtons = 0;
    g_ownedSurveyButton = 0;
    g_previousState = zonai_survey::feature::ScanState::Idle;
}


void serviceSurveyInput(const totk::engine::NpadFrame& frame, std::uint64_t buttons,
                        std::uint64_t pressed) {

    std::uint64_t ownedMask = BTN_UP;
    if ((buttons & BTN_ZL) != 0) g_ownedSurveyButton |= buttons & ownedMask;
    frame.maskOwnedButtons(g_ownedSurveyButton);

    if ((buttons & BTN_ZL) == 0 || (pressed & BTN_UP) == 0) return;
    const auto verdict = zonai_survey::integration::triggerSurvey();
    if (verdict == zonai_survey::pure::ScanVerdict::CoolingDown) return;
    if (verdict != zonai_survey::pure::ScanVerdict::Accepted) {
        zonai_survey::render::atlasBanner("Cannot survey here",
                            zonai_survey::presentation::displayText(verdict), 150);
        return;
    }
}

void serviceSurveyCompletion() {
    {
        zonai_survey::engine::perf::Timer scanTimer(zonai_survey::engine::perf::gameScan);
        g_scan.tick();
    }

    {
        zonai_survey::engine::perf::Timer glyphTimer(zonai_survey::engine::perf::gameGlyphs);
        g_glyphs.tick();
    }

    const auto state = g_scan.state();
    if (state == zonai_survey::feature::ScanState::Idle && g_previousState != state)
        g_glyphs.clear();
    if (state == zonai_survey::feature::ScanState::Holding &&
        g_previousState == zonai_survey::feature::ScanState::Pulsing) {

        const auto& glyphs = g_glyphs.diagnostics();
        const auto& cut = glyphs.filtered;
        Logging.Log(
            "[zonai-survey] glyphs published=%u map=%u roster=%u walked=%u hits=%u live_ok=%d "
            "farthest_live=%dm drawn=%u offscreen=%u",
            glyphs.published, glyphs.fromMap, glyphs.fromRoster, glyphs.rosterWalked,
            glyphs.rosterHits, glyphs.rosterAvailable ? 1 : 0,
            static_cast<int>(glyphs.farthestLiveMeters),
            zonai_survey::render::lastGlyphsDrawn(),
            zonai_survey::render::lastGlyphsOffscreen());
        Logging.Log(
            "[zonai-survey] glyph filters cone=%u cap=%u on_player=%u duplicate_of_live=%u "
            "names_collided=%u",
            cut.outsideCone, cut.lostToCap, cut.onPlayer, cut.overridden,
            zonai_survey::render::lastNamesDropped());
    }
    g_previousState = state;
}

void flushGamePerf() {}

void moduleTick(void* npadDevice) {

    if (!g_ready) return;

    flushGamePerf();
    zonai_survey::engine::perf::Timer tickTimer(zonai_survey::engine::perf::gameTick);

    if (npadDevice) {
        const auto frame = g_npad.read(npadDevice);
        if (!SURVEY_TUNING || frame.snapshot().freshSampleCount) {
            const std::uint64_t buttons = frame.snapshot().buttons;
            const std::uint64_t pressed = buttons & ~g_previousButtons;
            g_previousButtons = buttons;
            g_ownedSurveyButton &= buttons;
            serviceSurveyInput(frame, buttons, pressed);
        }
    }
    serviceSurveyCompletion();
}

bool moduleRequestExit() { return true; }


void moduleOnRaycast(wwpg::RaycastFn original, const void*, const void*, const void* object,
                     const void*, std::uint32_t, std::uint32_t) {

    namespace perf = zonai_survey::engine::perf;
    const bool sampled = (perf::workerSeen.fetch_add(1, std::memory_order_relaxed) & 15u) == 0;
    const std::uint64_t probeStart = sampled ? perf::now() : 0;
    g_scan.serviceProbes(reinterpret_cast<zonai_survey::engine::RaycastFn>(original), object);
    if (sampled) perf::addWorkerSample(perf::now() - probeStart);
}

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
     &moduleOnRaycast,
     nullptr,
};

}

namespace zonai_survey::integration {

pure::ScanVerdict triggerSurvey() {
    const auto now = svcGetSystemTick();
    if (g_cooldown.remaining(now)) {
        if (!g_refusalSound.remaining(now)) {
            const bool available = audio::playCue(pure::kSurveyRefusalCue);
            g_refusalSound.started = now;
            g_refusalSound.duration = pure::kSystemTicksPerSecond / 4;
            Logging.Log("[survey-options] cooldown refusal sound_bank=%u remaining_ticks=%llu\n",
                unsigned(available), g_cooldown.remaining(now));
        }
        return pure::ScanVerdict::CoolingDown;
    }
    const pure::ScanVerdict verdict = g_scan.trigger();
    if (verdict != pure::ScanVerdict::Accepted) return verdict;

    g_cooldown.start(svcGetSystemTick(), options::cooldownSeconds());
    Logging.Log("[survey-options] accepted range=%.1f cooldown=%u tuning=%u\n",
        options::nextRange(), options::cooldownSeconds(), unsigned(SURVEY_TUNING));
    g_glyphs.onPulse(g_scan.originX(), g_scan.originY(), g_scan.originZ(),
                     g_scan.headingX(), g_scan.headingZ(), g_scan.sceneGeneration());
    return verdict;
}

}

namespace wwpg::modules {
const Module& zonaiSurvey() { return kModule; }
}
