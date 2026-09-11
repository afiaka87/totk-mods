#pragma once

#include "RecallGameTime.hpp"
#include "RecallPoseHistory.hpp"
#include "RecallPlaybackSpeed.hpp"
#include "RecallPosePresentation.hpp"

namespace self_recall::pure {

enum class PosePlaybackStatus : std::uint8_t {
    Ready, Held, AtEnd, NoHistory, TooShort, InvalidClock,
    UnavailableFrame, WrongWorld, UnsafeSample, InvalidSpeed,
};

class PosePlayback {
public:
    PosePlaybackStatus begin(const PoseHistory& history, std::uint32_t world,
                             const GameTimeSnapshot& clock) {
        reset();
        if (clock.status != GameTimeStatus::Running) return PosePlaybackStatus::InvalidClock;
        auto latest = history.newest();
        const auto count = history.count();
        if (!latest || !count) return PosePlaybackStatus::NoHistory;
        const auto& newest = latest.get()->header;
        if (!world || newest.worldGeneration != world) return PosePlaybackStatus::WrongWorld;
        if (!(newest.route.flags & SampleAdmissible)) return PosePlaybackStatus::UnsafeSample;
        auto oldest = history.before(newest.key, count - 1);
        if (!oldest) return PosePlaybackStatus::UnavailableFrame;
        const auto firstTime = oldest.get()->header.elapsedNanoseconds;
        if (newest.elapsedNanoseconds < firstTime ||
            newest.elapsedNanoseconds > clock.elapsedNanoseconds)
            return PosePlaybackStatus::InvalidClock;
        const auto duration = newest.elapsedNanoseconds - firstTime;
        if (duration < kRecallMinimumNanoseconds) return PosePlaybackStatus::TooShort;
        if (duration > kRecallWindowNanoseconds) return PosePlaybackStatus::UnavailableFrame;
        history_ = &history;
        anchor_ = newest.key;
        count_ = count;
        world_ = world;
        newestTime_ = newest.elapsedNanoseconds;
        duration_ = duration;
        lastClock_ = clock.elapsedNanoseconds;
        selected_ = std::move(latest);
        presentation_ = {newest.key, {}};
        return PosePlaybackStatus::Ready;
    }

    PosePlaybackStatus step(const GameTimeSnapshot& clock, std::uint32_t world,
                            std::uint32_t allowedThrough = UINT32_MAX,
                            PlaybackRate rate = PlaybackRate::Normal) {
        if (!history_ || !selected_) return PosePlaybackStatus::NoHistory;
        if (world != world_) return PosePlaybackStatus::WrongWorld;
        if (history_->generation() != anchor_.generation) return PosePlaybackStatus::UnavailableFrame;
        if (!validPlaybackRate(rate)) return PosePlaybackStatus::InvalidSpeed;
        if (clock.elapsedNanoseconds < lastClock_) return PosePlaybackStatus::InvalidClock;
        if (clock.status == GameTimeStatus::Paused || clock.status == GameTimeStatus::NoAdvance) {
            lastClock_ = clock.elapsedNanoseconds;
            return PosePlaybackStatus::Held;
        }
        if (clock.status != GameTimeStatus::Running) return PosePlaybackStatus::InvalidClock;
        const auto delta = clock.elapsedNanoseconds - lastClock_;
        lastClock_ = clock.elapsedNanoseconds;
        const auto remaining = duration_ - elapsed_;
        const auto bounded = delta < remaining ? delta : remaining;
        const auto scaled = bounded * static_cast<unsigned>(rate) + rateRemainder_;
        const auto advance = scaled / 4;
        rateRemainder_ = static_cast<std::uint8_t>(scaled % 4);
        elapsed_ = advance >= remaining ? duration_ : elapsed_ + advance;
        if (allowedThrough < count_ - 1) {
            if (allowedThrough < index_) return PosePlaybackStatus::UnavailableFrame;
            PoseFrameHeader boundary;
            if (!headerAt(allowedThrough, boundary)) return PosePlaybackStatus::UnavailableFrame;
            const auto limit = newestTime_ - boundary.elapsedNanoseconds;
            if (elapsed_ >= limit) { elapsed_ = limit; rateRemainder_ = 0; }
        }
        const auto target = newestTime_ - elapsed_;
        std::uint32_t low = index_;
        std::uint32_t high = count_ - 1;
        while (low < high) {
            const auto middle = low + (high - low + 1) / 2;
            PoseFrameHeader frame;
            if (!headerAt(middle, frame)) return PosePlaybackStatus::UnavailableFrame;
            const auto recordedTime = frame.elapsedNanoseconds;
            const auto tolerance = (static_cast<unsigned>(rate) + 3u) / 4u;
            if (recordedTime >= target || target - recordedTime <= tolerance) low = middle;
            else high = middle - 1;
        }
        for (auto next = index_ + 1; next <= low; ++next) {
            PoseFrameHeader frame;
            if (!headerAt(next, frame)) return PosePlaybackStatus::UnavailableFrame;
            if (!(frame.route.flags & SampleAdmissible))
                return PosePlaybackStatus::UnsafeSample;
        }
        const bool changed = low != index_;
        auto frame = history_->before(anchor_, low);
        if (!frame) return PosePlaybackStatus::UnavailableFrame;
        selected_ = std::move(frame);
        index_ = low;
        presentation_ = {selectedKey(), {}};
        const auto& current = selected_.get()->header;
        if (target < current.elapsedNanoseconds && index_ + 1 < count_ && index_ < allowedThrough) {
            PoseFrameHeader older;
            if (!headerAt(index_ + 1, older)) return PosePlaybackStatus::UnavailableFrame;
            if (!(older.route.flags & SampleAdmissible)) return PosePlaybackStatus::UnsafeSample;
            if (!interpolatePosition(current, older, target, presentation_))
                return PosePlaybackStatus::UnavailableFrame;
        }
        if (index_ + 1 == count_) return PosePlaybackStatus::AtEnd;
        return changed ? PosePlaybackStatus::Ready : PosePlaybackStatus::Held;
    }

    bool hold(const GameTimeSnapshot& clock) {
        if (clock.elapsedNanoseconds < lastClock_) return false;
        lastClock_ = clock.elapsedNanoseconds;
        return true;
    }

    void reset() {
        selected_.release();
        history_ = nullptr;
        anchor_ = {};
        count_ = index_ = world_ = 0;
        newestTime_ = duration_ = elapsed_ = lastClock_ = 0;
        rateRemainder_ = 0;
        presentation_ = {};
    }
    const RecordedPoseFrame* selectedFrame() const { return selected_.get(); }
    PoseFrameKey selectedKey() const { return selected_ ? selected_.get()->header.key : PoseFrameKey{}; }
    PosePresentation presentation() const { return presentation_; }
    HistorySample appliedSample() const {
        auto sample = selected_ ? selected_.get()->header.route : HistorySample{};
        shiftPosition(sample.pose.position, presentation_.offset);
        return sample;
    }
    PoseFrameKey anchorKey() const { return anchor_; }
    PoseReadLease frameAt(std::uint32_t index) const {
        return history_ && index < count_ ? history_->before(anchor_, index) : PoseReadLease{};
    }
    bool headerAt(std::uint32_t index, PoseFrameHeader& out) const {
        return history_ && index < count_ && history_->copyHeaderBefore(anchor_, index, out);
    }
    std::uint32_t count() const { return count_; }
    std::uint32_t index() const { return index_; }
    std::uint64_t durationNanoseconds() const { return duration_; }
    std::uint64_t elapsedNanoseconds() const { return elapsed_; }

private:
    const PoseHistory* history_ = nullptr;
    PoseReadLease selected_;
    PoseFrameKey anchor_{};
    PosePresentation presentation_{};
    std::uint32_t count_ = 0;
    std::uint32_t index_ = 0;
    std::uint32_t world_ = 0;
    std::uint64_t newestTime_ = 0;
    std::uint64_t duration_ = 0;
    std::uint64_t elapsed_ = 0;
    std::uint64_t lastClock_ = 0;
    std::uint8_t rateRemainder_ = 0;
};

}  // namespace self_recall::pure
