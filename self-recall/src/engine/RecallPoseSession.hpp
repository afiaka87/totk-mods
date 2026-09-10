#pragma once

#include "RecallPosePlayback.hpp"

namespace self_recall::pose_session {

struct BeginResult {
    bool pending = false;
    pure::PosePlaybackStatus status = pure::PosePlaybackStatus::NoHistory;
};

void initialize();
BeginResult begin(std::uint32_t world, const pure::GameTimeSnapshot& clock);
pure::PosePlayback* playback(); // gameplay-thread access only
bool publishSelected();
void reset(bool clearHistory = true);

pure::PoseReadLease acquireSelected(pure::PosePresentation* presentation = nullptr);
pure::PoseReadLease acquirePresentation(pure::PosePresentation* presentation = nullptr);
void latchPresentation(const pure::GameTimeSnapshot& clock);
bool active();
bool publishApplied(std::uintptr_t player, std::uint32_t actorId, std::uint32_t world,
                    const pure::HistorySample& sample);
bool appliedClimb(std::uintptr_t player, std::uint32_t actorId, std::uint32_t world, pure::Pose& out);
bool appliedPose(std::uintptr_t player, std::uint32_t actorId, std::uint32_t world, pure::Pose& out);

}  // namespace self_recall::pose_session
