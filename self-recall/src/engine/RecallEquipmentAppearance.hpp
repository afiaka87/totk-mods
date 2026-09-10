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
} // namespace self_recall::equipment
