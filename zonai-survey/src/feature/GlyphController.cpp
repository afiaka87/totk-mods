// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis
#include <lib.hpp>

#include "GlyphController.hpp"
#include "PulsePolicy.hpp"
#include "PulseRenderer.hpp"

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
constexpr std::uint32_t kRevealWindowTicks = 600;

float distanceSq(float ax, float az, float bx, float bz) {
    const float dx = ax - bx;
    const float dz = az - bz;
    return dx * dx + dz * dz;
}

render::GlyphDraw drawOf(const pure::Glyph& glyph, float alpha, std::uint16_t flags) {
    return {glyph.x, glyph.y, glyph.z, glyph.distanceSq, alpha, glyph.name, flags, glyph.cls, glyph.icon};
}

}

void GlyphController::initialize(std::uintptr_t mainBase) {
    mainBase_ = mainBase;
    clear();
}

void GlyphController::forget() {
    mapCount_ = 0;
    liveCount_ = 0;
    candidateCount_ = 0;
}

void GlyphController::clear() {
    forget();
    active_ = false;
    tick_ = 0;
    render::clearGlyphs();
}

void GlyphController::onPulse(float originX, float originY, float originZ,
                              float headingX, float headingZ,
                              std::uint32_t sceneGeneration) {
    if (sceneGeneration != scene_) {
        forget();
        scene_ = sceneGeneration;
    }
    originX_ = originX;
    originY_ = originY;
    originZ_ = originZ;
    headingX_ = headingX;
    headingZ_ = headingZ;
#if SURVEY_CONSTRAINED
    range_ = options::nextRange();
#endif
    active_ = true;
    pulseTick_ = tick_;

    gatherFromMap();
    lastRosterTick_ = tick_;
    rebuildRosterCandidates();
    refreshLivePositions();
}

std::uint32_t GlyphController::revealTick(const pure::Glyph& glyph) const {
    const float forward = (glyph.x - originX_) * headingX_ + (glyph.z - originZ_) * headingZ_;
    const float ticks = pulse::markerArrivalSeconds(forward > 0.0f ? forward : 0.0f) * 60.0f;
    return pulseTick_ + (ticks > 0.0f ? static_cast<std::uint32_t>(ticks) : 0);
}

void GlyphController::gatherFromMap() {
    pure::GlyphPicker picker;
    picker.reset();
    const float cosHalf = pulse::scanConeCosHalf();
    pure::forEachPlacementNear(
        originX_, originZ_, scanRange(),
        [&](const glyphs::Placement& placement, float d2) {
            const float wx = pure::placementWorldX(placement);
            const float wz = pure::placementWorldZ(placement);
            if (!pure::withinCone(wx - originX_, wz - originZ_, headingX_, headingZ_, cosHalf)) return;
            pure::Glyph glyph{};
            glyph.x = wx;
            glyph.y = pure::placementWorldY(placement);
            glyph.z = wz;
            glyph.distanceSq = d2;
            glyph.name = placement.name;
            glyph.flags = placement.flags;
            glyph.cls = static_cast<std::uint8_t>(pure::glyphClassOf(placement.name));
            glyph.icon = pure::glyphIconOf(placement.name);
            picker.offer(glyph);
        });

    // Keep map marks nearest-first so the nearest win when the frame fills.
    mapCount_ = 0;
    for (const pure::Glyph& glyph : picker) {
        std::uint32_t at = mapCount_++;
        for (; at > 0 && map_[at - 1].glyph.distanceSq > glyph.distanceSq; --at) map_[at] = map_[at - 1];
        map_[at] = {glyph, revealTick(glyph)};
    }
}

void GlyphController::rebuildRosterCandidates() {
    candidateCount_ = 0;
    if (!mainBase_) return;

    const auto scene = te::resolveScene(mainBase_);
    if (!scene.succeeded || !scene.value.isReady()) return;

    const auto roster = te::resolveActiveProcessRoster(mainBase_, scene.value.token);
    if (!roster.succeeded) return;

    const auto walk = te::visitActiveProcesses(
        roster.value, [&](const te::ActiveProcessView& process) {
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
    if (!walk.succeeded) candidateCount_ = 0;
}

void GlyphController::refreshLivePositions() {
    liveCount_ = 0;
    if (!mainBase_ || candidateCount_ == 0) return;

    const auto scene = te::resolveScene(mainBase_);
    if (!scene.succeeded || !scene.value.isReady()) return;
    if (static_cast<std::uint32_t>(scene.value.token.value) != scene_) return;

    const te::TransformService transforms{te::TransformFunctions{}};

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

    const float cosHalf = pulse::scanConeCosHalf();
    pure::GlyphPicker picker;
    picker.reset();
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
        // Held or worn items sit on Link; they are not discoveries.
        if (distanceSq(px, pz, playerX, playerZ) <= kSelfRadiusMeters * kSelfRadiusMeters) continue;
        if (!pure::withinSurveyRange(px-originX_, pz-originZ_, scanRange())) continue;
        if (!pure::withinCone(px - originX_, pz - originZ_, headingX_, headingZ_, cosHalf)) continue;

        pure::Glyph glyph{};
        glyph.x = px;
        glyph.y = pose.value.position.y;
        glyph.z = pz;
        glyph.distanceSq = distanceSq(px, pz, originX_, originZ_);
        glyph.name = candidate.name;
        glyph.cls = candidate.cls;
        glyph.icon = pure::glyphIconOf(candidate.name);
        picker.offer(glyph);
    }
    for (const pure::Glyph& glyph : picker) live_[liveCount_++] = glyph;
}

void GlyphController::tick() {
    if (!active_) return;
    tick_ = pulseTick_ + static_cast<std::uint32_t>(pulse::pulseSeconds() * 60.0f);

    if (tick_ - pulseTick_ <= kRevealWindowTicks + pure::kGlyphLifeTicks) {
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
        const std::uint32_t reveal = revealTick(live_[i]);
        if (tick_ < reveal) continue;
        const float alpha = pure::glyphAlpha(tick_ - reveal);
        if (alpha > 0.0f) frame.glyphs[frame.count++] = drawOf(live_[i], alpha, 0);
    }
    const std::uint32_t liveInFrame = frame.count;

    // A live actor replaces the map mark at the same spot.
    for (std::uint32_t i = 0; i < mapCount_ && frame.count < pure::kMaxGlyphs; ++i) {
        const MapMark& mark = map_[i];
        if (tick_ < mark.revealTick) continue;
        const float alpha = pure::glyphAlpha(tick_ - mark.revealTick);
        if (!(alpha > 0.0f)) continue;
        bool duplicated = false;
        for (std::uint32_t k = 0; k < liveInFrame && !duplicated; ++k) {
            const render::GlyphDraw& live = frame.glyphs[k];
            duplicated = distanceSq(live.x, live.z, mark.glyph.x, mark.glyph.z) <= kLiveMatchMeters * kLiveMatchMeters;
        }
        if (!duplicated) frame.glyphs[frame.count++] = drawOf(mark.glyph, alpha, mark.glyph.flags);
    }

    if (frame.count == 0 && tick_ - pulseTick_ > kRevealWindowTicks + pure::kGlyphLifeTicks) {
        active_ = false;
        forget();
    }
    render::publishGlyphs(frame);
}

}
