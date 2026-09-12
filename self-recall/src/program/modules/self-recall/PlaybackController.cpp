
#include "SelfRecallModule.hpp"

#include <nn/util.h>

#include "RecallRuntimeEngine.hpp"
#include "RecallModelEngine.hpp"

namespace self_recall::playback {
namespace {

bool applySample(RecallRuntime& runtime, const pure::HistorySample& sample) {
    auto& playback = runtime.playback;
    world::PlayerIdentity player;
    if (!world::readPlayerIdentity(player)) return false;
    if (!pose_session::publishApplied(player.address, player.incarnation,
                                     runtime.session.worldGeneration, sample)) return false;
    if (!world::forcePose(sample.pose)) return false;
    playback.lastApplied = sample.pose;
    playback.lastSampleFlags = sample.flags;
    playback.lastAppliedRecordedSpeed = sample.pathSpeed;
    playback.lastAppliedRate = runtime.session.speed.rate();
    playback.lastAppliedEngineSpeed = std::sqrt(
        sample.engineVelocity[0] * sample.engineVelocity[0] +
        sample.engineVelocity[1] * sample.engineVelocity[1] +
        sample.engineVelocity[2] * sample.engineVelocity[2]);
    playback.haveApplied = true;
    return true;
}

bool verifyAppliedPose(RecallRuntime& runtime, pure::PosePlayback* cursor,
                       const pure::GameTimeSnapshot& clock) {
    auto& playback = runtime.playback;
    if (!playback.haveApplied) return true;
    pure::Pose now{};
    if (!world::readPose(now)) {
        finish(runtime, self_recall::pure::PlaybackStop::NonfiniteCurrent,
               true);
        return false;
    }
    float displacement =
        pure::distance3(now.position, playback.lastApplied.position);
    world::PlayerIdentity player;
    pure::Pose shown;
    if (world::readPlayerIdentity(player) &&
        pose_session::appliedPose(player.address, player.incarnation, runtime.session.worldGeneration, shown)) {
        const auto shownDisplacement = pure::distance3(now.position, shown.position);
        if (shownDisplacement < displacement) displacement = shownDisplacement;
    }
    const float liveSpeed = world::liveSpeed();
    const float appliedScale = static_cast<unsigned>(playback.lastAppliedRate) / 4.0f;
    const float allowance = pure::correctionAllowanceMeters(
        playback.lastAppliedRecordedSpeed * appliedScale, playback.lastAppliedEngineSpeed * appliedScale,
        liveSpeed);
    if (!std::isfinite(displacement) || displacement > allowance) {
        ++playback.fightTicks;
        SRLOG(
            "REWIND_FIGHT n=%d displaced_cm=%d allowance_cm=%d "
            "path_dms=%d engine_dms=%d live_dms=%d speed=%s",
            playback.fightTicks,
            std::isfinite(displacement) ? (int)(displacement * 100.0f) : -1,
            (int)(allowance * 100.0f),
            (int)(playback.lastAppliedRecordedSpeed * 10.0f),
            (int)(playback.lastAppliedEngineSpeed * 10.0f),
            (int)(liveSpeed * 10.0f), pure::playbackRateText(playback.lastAppliedRate));
        if (playback.fightTicks >= pure::kBlockPersistTicks) {
            finish(runtime, self_recall::pure::PlaybackStop::Blocked,
                   true);
            return false;
        }
        (void)cursor->hold(clock);
        if (!world::forcePose(playback.lastApplied))
            finish(runtime, self_recall::pure::PlaybackStop::PoseApplyFailed, true);
        return false;
    }
    playback.fightTicks = 0;
    playback.lastVerified = now;
    playback.haveVerified = true;
    playback.haveApplied = false;
    return true;
}

void selectAndApplyFrame(RecallRuntime& runtime, pure::PosePlayback* cursor,
                         const pure::GameTimeSnapshot& clock) {
    auto& playback = runtime.playback;
    if (!playback.selectionPending) {
        const auto selected = cursor->step(clock, runtime.session.worldGeneration,
                                           runtime.safety.probe.evaluatedThrough, runtime.session.speed.rate());
        if (selected != pure::PosePlaybackStatus::Ready &&
            selected != pure::PosePlaybackStatus::Held &&
            selected != pure::PosePlaybackStatus::AtEnd) {
            const bool unsafe = selected == pure::PosePlaybackStatus::UnsafeSample;
            SRLOG("pose playback stopped: status=%u", static_cast<unsigned>(selected));
            finish(runtime, unsafe ? pure::PlaybackStop::UnsafeSample : pure::PlaybackStop::PoseUnavailable, true);
            return;
        }
        playback.selectedEnd = selected == pure::PosePlaybackStatus::AtEnd;
    } else {
        (void)cursor->hold(clock);
    }
    if (!pose_session::publishSelected()) {
        playback.selectionPending = true;
        if (playback.haveVerified) {
            if (!world::forcePose(playback.lastVerified)) {
                finish(runtime, self_recall::pure::PlaybackStop::PoseApplyFailed, true);
                return;
            }
            playback.lastApplied = playback.lastVerified;
            playback.haveApplied = true;
        }
        return;
    }
    playback.selectionPending = false;
    const auto* frame = cursor->selectedFrame();
    if (!frame) {
        finish(runtime, self_recall::pure::PlaybackStop::PoseUnavailable, true);
        return;
    }
    const pure::HistorySample sample = cursor->appliedSample();
    if (!(sample.flags & pure::SampleAdmissible)) {
        finish(runtime, self_recall::pure::PlaybackStop::UnsafeSample,
               true);
        return;
    }
    if (!pure::finitePose(sample.pose)) {
        finish(runtime, self_recall::pure::PlaybackStop::NonfiniteSample,
               true);
        return;
    }

    if (!applySample(runtime, sample)) {
        finish(runtime, self_recall::pure::PlaybackStop::PoseApplyFailed, true);
        return;
    }
    playback.rewindRemaining = static_cast<std::uint16_t>(cursor->count() - cursor->index() - 1);
    safety::armProbe(runtime);
}

}

void clearVelocity(const char* reason) {
    if (!world::clearLinearVelocity())
        SRLOG("VELOCITY_CLEAR skipped reason=%s no-player-component",
              reason ? reason : "unspecified");
}

void start(RecallRuntime& runtime) {
    auto& playback = runtime.playback;
    if (playback.rewinding) return;
    glider_release::cancel(pure::GliderReleaseEnd::NewRecall);

    const auto stamina = native_gameplay::stamina(world::playerActor());
    if (stamina != pure::StaminaStatus::Available) {
        playback.startPending = false;
        pose_session::reset(false);
        setEvent(stamina == pure::StaminaStatus::Empty ? "not enough stamina" : "stamina unavailable");
        SRLOG("start refused: stamina=%u", static_cast<unsigned>(stamina));
        return;
    }

    if (runtime.safety.unsafeNow != Unsafe::None) {
        pose_session::reset(false);
        playback.startPending = false;
        char buffer[96];
        if (runtime.safety.unsafeNow == Unsafe::SpecialState)
            nn::util::SNPrintf(buffer, sizeof(buffer),
                               "refused: special move (state %u)",
                               runtime.safety.uiStateRaw);
        else
            nn::util::SNPrintf(buffer, sizeof(buffer), "refused: %s",
                               unsafeName(runtime.safety.unsafeNow));
        setEvent(buffer);
        SRLOG("start refused: %s ui_state=%u",
              unsafeName(runtime.safety.unsafeNow), runtime.safety.uiStateRaw);
        return;
    }

    pure::Pose current{};
    if (!world::readPose(current)) {
        pose_session::reset(false);
        playback.startPending = false;
        setEvent("refused: player pose invalid");
        SRLOG("start refused: non-finite current pose");
        return;
    }

    pure::GameTimeSnapshot clock;
    if (!game_clock::snapshot(clock)) {
        pose_recorder::logDiagnostics();
        playback.startPending = true;
        return;
    }
    const auto begun = pose_session::begin(runtime.session.worldGeneration, clock);
    playback.startPending = begun.pending;
    if (begun.pending) { setEvent("starting Recall"); return; }
    if (begun.status != pure::PosePlaybackStatus::Ready) {
        setEvent(begun.status == pure::PosePlaybackStatus::TooShort
                     ? "not enough animation history yet"
                     : "animation history is not ready");
        SRLOG("start refused: pose_history=%u", static_cast<unsigned>(begun.status));
        pose_recorder::logDiagnostics();
        return;
    }
    auto& cursor = *pose_session::playback();

    playback.rewinding = true;
    playback.fightTicks = 0;
    runtime.safety.probe.obstructed = false;
    playback.haveApplied = false;
    playback.haveVerified = false;
    playback.rewindRemaining = playback.rewindTotal = static_cast<std::uint16_t>(cursor.count());
    playback.selectionPending = true;
    playback.selectedEnd = false;
    playback.lastStepClockSerial = 0;
    clearVelocity("rewind start");
    if (!native_gameplay::begin(world::playerActor())) {
        finish(runtime, pure::PlaybackStop::StaminaUnavailable, false);
        return;
    }
    if (!applySample(runtime, cursor.appliedSample())) {
        finish(runtime, self_recall::pure::PlaybackStop::PoseApplyFailed, false);
        return;
    }
    const bool presentation = effects::startRewind();
    effects::buildRoute();
    setEvent("REWINDING - B cancels");

    SRLOG("REWIND START frames=%u duration_ms=%llu generation=%u present=%u speed=%s",
          static_cast<unsigned>(cursor.count()),
          static_cast<unsigned long long>(cursor.durationNanoseconds() / 1000000),
          cursor.anchorKey().generation, static_cast<unsigned>(presentation),
          pure::playbackRateText(runtime.session.speed.rate()));
}

void finish(RecallRuntime& runtime, pure::PlaybackStop stop,
            bool emitEnd) {
    auto& playback = runtime.playback;
    const unsigned completed =
        playback.rewindTotal >= playback.rewindRemaining
            ? static_cast<unsigned>(playback.rewindTotal -
                                    playback.rewindRemaining)
            : 0u;
    const unsigned total = playback.rewindTotal;
    const auto text = pure::playbackStopText(stop);
    const auto* reason = text.reason;
    const auto exit = pure::playbackExitPlan(stop, runtime.safety.unsafeNow == Unsafe::None,
                                          playback.lastSampleFlags);

    native_gameplay::release();
    effects::stopForExit(reason, emitEnd);
    clearVelocity(reason);
    SRLOG("REWIND %s completed=%u/%u", reason ? reason : "STOP", completed,
          total);
    clearRoute(reason);
    if (exit.releaseGlider) glider_release::request(world::playerActor(), runtime.session.worldGeneration,
                                               runtime.session.tick, true);
    setEvent(text.event);
}

void step(RecallRuntime& runtime) {
    auto& playback = runtime.playback;
    if (!playback.rewinding || !world::havePlayer()) return;

    pure::GameTimeSnapshot clock;
    auto* cursor = pose_session::playback();
    if (!cursor || !game_clock::snapshot(clock)) return;
    if (clock.status != pure::GameTimeStatus::Running &&
        clock.status != pure::GameTimeStatus::Paused &&
        clock.status != pure::GameTimeStatus::NoAdvance) {
        finish(runtime, self_recall::pure::PlaybackStop::ClockUnavailable, true);
        return;
    }
    if (clock.status != pure::GameTimeStatus::Running) {
        (void)cursor->hold(clock);
        return;
    }
    if (clock.serial == playback.lastStepClockSerial) return;
    playback.lastStepClockSerial = clock.serial;
    const auto stamina = native_gameplay::stamina(world::playerActor());
    if (stamina != pure::StaminaStatus::Available) {
        finish(runtime, stamina == pure::StaminaStatus::Empty ? pure::PlaybackStop::StaminaExhausted
                                                             : pure::PlaybackStop::StaminaUnavailable, true);
        return;
    }
    if (!pose_render::ready(cursor->selectedKey().generation)) {
        (void)cursor->hold(clock);
        return;
    }

    world::clearLinearVelocity();

    if (runtime.safety.probe.obstructed && cursor->index() >= runtime.safety.probe.evaluatedThrough) {
        const auto& hit = runtime.safety.probe.hitPosition;
        SRLOG("ROUTE_RAY_BLOCKED hit_cm=(%d,%d,%d)", (int)(hit.x * 100.0f),
              (int)(hit.y * 100.0f), (int)(hit.z * 100.0f));
        finish(runtime, self_recall::pure::PlaybackStop::RayBlocked, true);
        return;
    }
    safety::armProbe(runtime);
    if (runtime.safety.probe.unavailable && cursor->index() >= runtime.safety.probe.evaluatedThrough) {
        SRLOG("ROUTE_RAY_UNAVAILABLE index=%u evaluated=%u timeouts=%u",
              cursor->index(), runtime.safety.probe.evaluatedThrough,
              runtime.safety.probe.timeouts);
        finish(runtime, self_recall::pure::PlaybackStop::RouteUnavailable, true);
        return;
    }

    if (!verifyAppliedPose(runtime, cursor, clock)) return;

    if (playback.selectedEnd && !playback.selectionPending) {
        finish(runtime, self_recall::pure::PlaybackStop::Finished, true);
        return;
    }

    selectAndApplyFrame(runtime, cursor, clock);
}

void applyRecordedInput(const totk::engine::NpadFrame& frame) {
    frame.writeOwnedLeftStick(0, 0);
}

}
