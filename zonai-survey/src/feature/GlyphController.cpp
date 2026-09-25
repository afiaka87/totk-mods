// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis
#include <lib.hpp>

#include "GlyphController.hpp"
#include "FidelityPolicy.hpp"
#include "FidelityPlayground.hpp"

#include "GlyphIndex.hpp"
#include "GlyphRenderer.hpp"
#include "PulseLattice.hpp"
#include "SurveyOptions.hpp"
#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/Scene.hpp"
#include "totk/engine/Transform.hpp"

namespace zonai_survey::feature {
namespace {

namespace te = totk::engine;

constexpr float kLiveMatchMeters = 3.0f;

constexpr float kSelfRadiusMeters = 1.5f;

constexpr std::uint32_t kRosterCadenceTicks = 20;
constexpr std::uint32_t kRevealWindowTicks = static_cast<std::uint32_t>(survey_fidelity::kScanSeconds * 60.0f);

float distanceSq(float ax, float az, float bx, float bz) {
    const float dx = ax - bx;
    const float dz = az - bz;
    return dx * dx + dz * dz;
}

std::uint32_t ticksUntilWaveReaches(float distanceMeters) {
    const float ticks = survey_fidelity::markerArrivalSeconds(distanceMeters) * 60.0f;
    if (!(ticks > 0.0f)) return 0;
    return static_cast<std::uint32_t>(ticks);
}

float waveDistanceMeters(float dx, float dz, float headingX, float headingZ) {
    if (!survey_fidelity::imprintEnabled()) return std::sqrt(dx*dx + dz*dz);
    const float forward = dx * headingX + dz * headingZ;
    return forward > 0.0f ? forward : 0.0f;
}

float coneCosHalfAngle() {
    return __builtin_cosf(pure::kSurveyWidthRadians * 0.5f);
}

}

void GlyphController::initialize(std::uintptr_t mainBase) {
    mainBase_ = mainBase;
    clear();
}

void GlyphController::clear() {
    mapCount_ = 0;
    liveCount_ = 0;
    candidateCount_ = 0;
    active_ = false;
    tick_ = 0;
    diagnostics_ = GlyphDiagnostics{};
    render::clearGlyphs();
}

void GlyphController::onPulse(float originX, float originY, float originZ,
                              float headingX, float headingZ,
                              std::uint32_t sceneGeneration) {
    if (sceneGeneration != scene_) {
        mapCount_ = 0;
        liveCount_ = 0;
        candidateCount_ = 0;
        scene_ = sceneGeneration;
    }
    originX_ = originX;
    originY_ = originY;
    originZ_ = originZ;
    headingX_ = headingX;
    headingZ_ = headingZ;
#if SURVEY_CONSTRAINED || SURVEY_TUNING
    range_ = options::nextRange();
#endif
    active_ = true;
    pulseTick_ = tick_;

    diagnostics_.filtered = GlyphFilterCounts{};
    gatherFromMap();
    lastRosterTick_ = tick_;
    rebuildRosterCandidates();
    refreshLivePositions();
}

void GlyphController::gatherFromMap() {
    pure::GlyphPicker picker;
    picker.reset();

    const float cosHalf = coneCosHalfAngle();
    std::uint32_t outsideCone = 0;

    pure::forEachPlacementNear(
        originX_, originZ_, scanRange(),
        [&](const glyphs::Placement& placement, float d2) {
            const float wx = pure::placementWorldX(placement);
            const float wz = pure::placementWorldZ(placement);
            if (!pure::withinCone(wx - originX_, wz - originZ_, headingX_, headingZ_, cosHalf)) {
                ++outsideCone;
                return;
            }

            const auto cls = pure::glyphClassOf(placement.name);

            pure::Glyph glyph{};
            glyph.x = wx;
            glyph.y = pure::placementWorldY(placement);
            glyph.z = wz;
            glyph.distanceSq = d2;
            glyph.name = placement.name;
            glyph.flags = placement.flags;
            glyph.cls = static_cast<std::uint8_t>(cls);
            glyph.icon = pure::glyphIconOf(placement.name);
            picker.offer(glyph);
        });

    mapCount_ = 0;
    for (const pure::Glyph& glyph : picker) {
        MapMark& mark = map_[mapCount_++];
        mark.glyph = glyph;
        mark.revealTick =
            pulseTick_ +
            ticksUntilWaveReaches(waveDistanceMeters(
                glyph.x - originX_, glyph.z - originZ_, headingX_, headingZ_));
    }

    diagnostics_.filtered.outsideCone = outsideCone;
    diagnostics_.filtered.lostToCap = picker.overflow();
    diagnostics_.fromMap = mapCount_;
}

void GlyphController::rebuildRosterCandidates() {
    candidateCount_ = 0;
    diagnostics_.rosterWalked = 0;
    diagnostics_.rosterAvailable = false;

    if (!mainBase_) return;

    const auto scene = te::resolveScene(mainBase_);
    if (!scene.succeeded || !scene.value.isReady()) return;

    const auto roster = te::resolveActiveProcessRoster(mainBase_, scene.value.token);
    if (!roster.succeeded) return;

    const auto walk = te::visitActiveProcesses(
        roster.value, [&](const te::ActiveProcessView& process) {
            ++diagnostics_.rosterWalked;

            const std::uint32_t name = pure::findGlyphName(process.processName.c_str());
            if (name == pure::kNoGlyphName) return te::VisitControl::Continue;
            if (candidateCount_ >= kMaxLiveCandidates) return te::VisitControl::Stop;

            LiveCandidate& candidate = candidates_[candidateCount_++];
            candidate.handle = process.handle;
            candidate.name = static_cast<std::uint16_t>(name);
            candidate.cls = static_cast<std::uint8_t>(pure::glyphClassOf(name));
            candidate.alive = true;
            return te::VisitControl::Continue;
        });

    if (!walk.succeeded) {
        candidateCount_ = 0;
        return;
    }
    diagnostics_.rosterAvailable = true;
}

void GlyphController::refreshLivePositions() {
    liveCount_ = 0;
    diagnostics_.rosterHits = 0;
    diagnostics_.farthestLiveMeters = 0.0f;

    if (!mainBase_ || candidateCount_ == 0) return;

    const auto scene = te::resolveScene(mainBase_);
    if (!scene.succeeded || !scene.value.isReady()) return;
    if (static_cast<std::uint32_t>(scene.value.token.value) != scene_) return;

    const te::TransformService transforms{te::TransformFunctions::fromMainBase(mainBase_)};

    float playerX = originX_;
    float playerZ = originZ_;
    const auto player = te::findResidentActor(scene.value, "Player");
    if (player.succeeded) {
        const auto pose = transforms.read(player.value, scene.value.token);
        if (pose.succeeded) {
            playerX = pose.value.position.x;
            playerZ = pose.value.position.z;
        }
    }

    const float cosHalf = coneCosHalfAngle();
    pure::GlyphPicker picker;
    picker.reset();
    std::uint32_t onPlayer = 0;
    std::uint32_t outsideCone = 0;

    for (std::uint32_t i = 0; i < candidateCount_; ++i) {
        LiveCandidate& candidate = candidates_[i];
        if (!candidate.alive) continue;

        const auto pose = transforms.read(candidate.handle, scene.value.token);
        if (!pose.succeeded) {

            candidate.alive = false;
            continue;
        }

        const float px = pose.value.position.x;
        const float pz = pose.value.position.z;

        if (distanceSq(px, pz, playerX, playerZ) <=
            kSelfRadiusMeters * kSelfRadiusMeters) {
            ++onPlayer;
            continue;
        }

        const float d2 = distanceSq(px, pz, originX_, originZ_);
#if SURVEY_CONSTRAINED || SURVEY_TUNING
        if (!pure::withinSurveyRange(px-originX_, pz-originZ_, scanRange())) continue;
#else
        if (d2 > pure::kMaxRange * pure::kMaxRange) continue;
#endif
        if (!pure::withinCone(px - originX_, pz - originZ_, headingX_, headingZ_, cosHalf)) {
            ++outsideCone;
            continue;
        }

        ++diagnostics_.rosterHits;
        const float metres = __builtin_sqrtf(d2);
        if (metres > diagnostics_.farthestLiveMeters) diagnostics_.farthestLiveMeters = metres;

        pure::Glyph glyph{};
        glyph.x = px;
        glyph.y = pose.value.position.y;
        glyph.z = pz;
        glyph.distanceSq = d2;
        glyph.name = candidate.name;
        glyph.cls = candidate.cls;
        glyph.icon = pure::glyphIconOf(candidate.name);
        picker.offer(glyph);
    }

    liveCount_ = 0;
    for (const pure::Glyph& glyph : picker) live_[liveCount_++] = glyph;

    diagnostics_.filtered.onPlayer = onPlayer;
    diagnostics_.filtered.outsideCone += outsideCone;
    diagnostics_.fromRoster = liveCount_;
}

void GlyphController::tick() {
    if (!active_) return;
    tick_ = pulseTick_ + static_cast<std::uint32_t>(survey_fidelity::pulseSeconds() * 60.0f);

    const std::uint32_t sincePulse = tick_ - pulseTick_;
    const bool stillLive = sincePulse <= kRevealWindowTicks + pure::kGlyphLifeTicks;

    if (stillLive) {
        if (tick_ - lastRosterTick_ >= kRosterCadenceTicks) {
            lastRosterTick_ = tick_;
            rebuildRosterCandidates();
        }

        refreshLivePositions();
    }

    publish();
}

void GlyphController::publish() {
    render::GlyphFrame frame{};
    frame.scene = scene_;

    for (std::uint32_t i = 0; i < liveCount_ && frame.count < pure::kMaxGlyphs; ++i) {
        const pure::Glyph& glyph = live_[i];
        const std::uint32_t reveal =
            pulseTick_ +
            ticksUntilWaveReaches(waveDistanceMeters(
                glyph.x - originX_, glyph.z - originZ_, headingX_, headingZ_));
        if (tick_ < reveal) continue;
        const float alpha = pure::glyphAlpha(tick_ - reveal);
        if (!(alpha > 0.0f)) continue;

        render::GlyphDraw& out = frame.glyphs[frame.count++];
        out.x = glyph.x;
        out.y = glyph.y;
        out.z = glyph.z;
        out.distanceSq = glyph.distanceSq;
        out.alpha = alpha;
        out.name = glyph.name;
        out.cls = glyph.cls;
        out.icon = glyph.icon;
        out.flags = 0;
    }
    const std::uint32_t liveInFrame = frame.count;

    std::uint32_t order[pure::kMaxGlyphs];
    for (std::uint32_t i = 0; i < mapCount_; ++i) order[i] = i;
    for (std::uint32_t i = 1; i < mapCount_; ++i) {
        const std::uint32_t key = order[i];
        std::uint32_t j = i;
        while (j > 0 && map_[order[j - 1]].glyph.distanceSq > map_[key].glyph.distanceSq) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = key;
    }

    std::uint32_t overridden = 0;
    for (std::uint32_t i = 0; i < mapCount_ && frame.count < pure::kMaxGlyphs; ++i) {
        const MapMark& mark = map_[order[i]];
        if (tick_ < mark.revealTick) continue;

        const float alpha = pure::glyphAlpha(tick_ - mark.revealTick);
        if (!(alpha > 0.0f)) continue;

        bool duplicated = false;
        for (std::uint32_t k = 0; k < liveInFrame; ++k) {
            const render::GlyphDraw& live = frame.glyphs[k];
            const float dx = live.x - mark.glyph.x;
            const float dz = live.z - mark.glyph.z;
            if (dx * dx + dz * dz <= kLiveMatchMeters * kLiveMatchMeters) {
                duplicated = true;
                break;
            }
        }
        if (duplicated) {
            ++overridden;
            continue;
        }

        render::GlyphDraw& out = frame.glyphs[frame.count++];
        out.x = mark.glyph.x;
        out.y = mark.glyph.y;
        out.z = mark.glyph.z;
        out.distanceSq = mark.glyph.distanceSq;
        out.alpha = alpha;
        out.name = mark.glyph.name;
        out.cls = mark.glyph.cls;
        out.icon = mark.glyph.icon;
        out.flags = mark.glyph.flags;
    }

    diagnostics_.filtered.overridden = overridden;
    diagnostics_.published = frame.count;

    if (frame.count == 0 && tick_ - pulseTick_ > kRevealWindowTicks + pure::kGlyphLifeTicks) {
        active_ = false;
        mapCount_ = 0;
        liveCount_ = 0;
        candidateCount_ = 0;
    }

    render::publishGlyphs(frame);
}

}
