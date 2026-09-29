// SPDX-License-Identifier: MIT
#include <lib.hpp>
#include "ScanController.hpp"
#include "PulseRenderer.hpp"
#include "CameraHeading.hpp"
#include "Audio.hpp"
#include "ScanPresentation.hpp"
#include "SurveyCue.hpp"
#include "SurveyOptions.hpp"
#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/Scene.hpp"
#include "totk/engine/Transform.hpp"

namespace zonai_survey::feature {
void ScanController::initialize(std::uintptr_t base) {
    mainBase_ = base;
    state_ = ScanState::Idle;
}
bool ScanController::resolveLink(float& x, float& y, float& z, std::uint32_t& generation) {
    namespace te = totk::engine;
    if (!mainBase_) return false;
    const auto scene = te::resolveScene(mainBase_);
    if (!scene.succeeded || !scene.value.isReady()) return false;
    const auto player = te::findResidentActor(scene.value, "Player");
    if (!player.succeeded) return false;
    const te::TransformService transforms{te::TransformFunctions{}};
    const auto pose = transforms.read(player.value, scene.value.token);
    if (!pose.succeeded) return false;
    x = pose.value.position.x; y = pose.value.position.y; z = pose.value.position.z;
    generation = static_cast<std::uint32_t>(scene.value.token.value);
    return true;
}
pure::ScanVerdict ScanController::trigger() {
    if (engine::cameraForwardValid() && resolveLink(originX_, originY_, originZ_, scene_)) {
        engine::cameraForward(headingX_, headingZ_);
        if (pulse::beginPulse(originX_, originY_, originZ_, headingX_, headingZ_)) {
            tick_ = 0; state_ = ScanState::Pulsing;
            audio::playCue(pure::kSurveyStartCueName);
            return pure::ScanVerdict::Accepted;
        }
    }
    Logging.Log("[survey-pulse] refused: %s\n", presentation::displayText(pure::ScanVerdict::PlayerUnresolved));
    return pure::ScanVerdict::PlayerUnresolved;
}
void ScanController::abandon(const char* why) {
    pulse::endPulse();
    state_ = ScanState::Idle;
    Logging.Log("[survey-pulse] scan ended: %s\n", why);
}
void ScanController::tick() {
    if (state_ == ScanState::Idle) return;
    float x{}, y{}, z{}; std::uint32_t scene{};
    if (!resolveLink(x, y, z, scene)) { abandon("lost Link mid-scan"); return; }
    if (scene != scene_) { abandon("the world reloaded"); return; }
    tick_ = static_cast<std::uint32_t>(pulse::pulseSeconds() * 60.0f);
    if (state_ == ScanState::Holding) return;
    if (!pulse::pulseRunning()) state_ = ScanState::Holding;
}
}
