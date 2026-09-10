
#include "RecallRuntime.hpp"

#include "RecallLog.hpp"
#include "RecallPoseSession.hpp"
#include "RecallPoseStorage.hpp"
#include "RecallGliderRelease.hpp"

namespace self_recall {
namespace {
RecallRuntime g_runtime{};

}  // namespace

RecallRuntime& runtime() { return g_runtime; }

const char* unsafeName(Unsafe unsafe) {
    switch (unsafe) {
        case Unsafe::None: return "ordinary";
        case Unsafe::Loading: return "loading";
        case Unsafe::Riding: return "riding";
        case Unsafe::SpecialState: return "special move";
        case Unsafe::Tumbling: return "tumbling";
        case Unsafe::StateUnreadable: return "state unreadable";
    }
    return "?";
}

void setEvent(const char* text) {
    char* event = g_runtime.session.event;
    int i = 0;
    for (; i < 95 && text && text[i]; ++i) event[i] = text[i];
    event[i] = 0;
}

void clearRoute(const char* reason) {
    auto& playback = g_runtime.playback;
    const auto* poses = pose_storage::history();
    const bool material = playback.rewinding || (poses && poses->count());

    pose_session::reset();
    glider_release::cancel();

    playback.rewinding = false;
    playback.startPending = false;
    playback.selectionPending = false;
    playback.selectedEnd = false;
    playback.lastStepClockSerial = 0;
    playback.startedNanoseconds = 0;
    playback.rewindIndex = 0;
    playback.rewindRemaining = 0;
    playback.rewindTotal = 0;
    playback.fightTicks = 0;
    playback.haveApplied = false;
    playback.lastSampleFlags = 0;
    playback.haveVerified = false;
    playback.lastAppliedRecordedSpeed = 0.0f;
    playback.lastAppliedEngineSpeed = 0.0f;
    playback.lastAppliedRate = pure::PlaybackRate::Normal;
    playback.replayMeters = 0.0f;
    playback.replayPeakRecordedSpeed = 0.0f;

    g_runtime.safety.probe.obstructed = false;
    probe::cancel(g_runtime.safety.probe);

    if (material) SRLOG("history cleared: %s", reason ? reason : "unspecified");
}

}  // namespace self_recall
