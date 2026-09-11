#pragma once
#include "RecallEquipmentAsset.hpp"

namespace self_recall::equipment {
void initializeAppearance(std::uintptr_t mainBase);
bool captureAppearance(detail::Asset& asset, unsigned assetIndex, std::span<const model::View> source);
bool restoreAppearance(unsigned token, std::span<detail::Asset> assets);
void releaseAppearance(unsigned token);
void beginRecord(std::uint32_t historyGeneration);
bool bindAppearanceFrame(pure::PoseFrameKey key, std::span<const unsigned> tokens);
unsigned appearanceToken(pure::PoseFrameKey key, unsigned model);
bool refuseAppearance(const char* reason, std::uint64_t detail, std::uint64_t extra);
void logAppearanceMemory();
void collectAppearance(const pure::PoseHistory& history);
bool captureBodyAppearance(const pure::RecordedPoseFrame& frame, std::span<unsigned> tokens);

// Restores CPU parameters before returning from the native model callback.
class BodyAppearanceScope {
public:
    BodyAppearanceScope(pure::PoseFrameKey key, unsigned modelIndex, const model::Identity& live);
    ~BodyAppearanceScope();
    BodyAppearanceScope(const BodyAppearanceScope&) = delete;
    BodyAppearanceScope& operator=(const BodyAppearanceScope&) = delete;
    bool ready() const { return buffer_ >= 0; }
private:
    int buffer_ = -1;
};
} // namespace self_recall::equipment
