// SPDX-License-Identifier: MIT
#include <lib.hpp>
#include "ScanController.hpp"
#include "FidelityPlayground.hpp"
#include "CameraHeading.hpp"
#include "Audio.hpp"
#include "SurveyOptions.hpp"
#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/Scene.hpp"
#include "totk/engine/Transform.hpp"

namespace zonai_survey::feature {
void ScanController::initialize(std::uintptr_t base) {
    mainBase_ = base;
    state_ = ScanState::Idle;
    diagnostics_ = {};
    Logging.Log("[survey-fidelity] controller_bytes=%u geometry_bytes=0 raycast_worker=disabled\n",
                unsigned(sizeof(*this)));
}
bool ScanController::resolveLink(float& x, float& y, float& z, std::uint32_t& generation) {
    namespace te = totk::engine;
    if (!mainBase_) return false;
    const auto scene = te::resolveScene(mainBase_);
    if (!scene.succeeded || !scene.value.isReady()) return false;
    const auto player = te::findResidentActor(scene.value, "Player");
    if (!player.succeeded) return false;
    const te::TransformService transforms{te::TransformFunctions::fromMainBase(mainBase_)};
    const auto pose = transforms.read(player.value, scene.value.token);
    if (!pose.succeeded) return false;
    x = pose.value.position.x; y = pose.value.position.y; z = pose.value.position.z;
    generation = static_cast<std::uint32_t>(scene.value.token.value);
    return true;
}
pure::ScanVerdict ScanController::trigger() {
    auto verdict = pure::ScanVerdict::Accepted;
    if (!engine::cameraForwardValid() || !resolveLink(originX_, originY_, originZ_, scene_))
        verdict = pure::ScanVerdict::PlayerUnresolved;
    else {
        engine::cameraForward(headingX_, headingZ_);
        if (!survey_fidelity::beginPulse(originX_, originY_, originZ_, headingX_, headingZ_))
            verdict = pure::ScanVerdict::PlayerUnresolved;
        else {
            tick_ = 0; state_ = ScanState::Pulsing;
            ++diagnostics_.pulses;
            audio::playCue(pure::kSurveyStartCueName);
        }
    }
    diagnostics_.lastVerdict = verdict;
    Logging.Log("[survey-fidelity] request verdict=%u scene=%u pulses=%u\n",
                unsigned(verdict), scene_, diagnostics_.pulses);
    return verdict;
}
void ScanController::abandon(pure::ScanAbandonReason reason) {
    survey_fidelity::endPulse();
    state_ = ScanState::Idle;
    diagnostics_.lastEnding = reason;
    Logging.Log("[survey-fidelity] scan end reason=%u elapsed_ticks=%u\n", unsigned(reason), tick_);
}
void ScanController::tick() {
    if (state_ == ScanState::Idle) return;
    float x{}, y{}, z{}; std::uint32_t scene{};
    if (!resolveLink(x, y, z, scene)) { abandon(pure::ScanAbandonReason::PlayerLost); return; }
    if (scene != scene_) { abandon(pure::ScanAbandonReason::WorldReloaded); return; }
    tick_ = static_cast<std::uint32_t>(survey_fidelity::pulseSeconds() * 60.0f);
    if (state_ == ScanState::Holding) return;
    if (!survey_fidelity::pulseRunning()) state_ = ScanState::Holding;
}
void ScanController::serviceProbes(engine::RaycastFn, const void*) {}
}
