#pragma once
#include "RecallPoseFrame.hpp"
#include <span>

#ifndef SELF_RECALL_CORPUS_CAPTURE
#define SELF_RECALL_CORPUS_CAPTURE 0
#endif

namespace self_recall::corpus {
#if SELF_RECALL_CORPUS_CAPTURE
void start();
void pose(const pure::RecordedPoseFrame& frame);
void appearance(unsigned token, std::span<const std::byte> bytes);
void binding(pure::PoseFrameKey key, std::span<const unsigned> tokens);
void schema(unsigned asset, unsigned properties, unsigned enums, unsigned names,
            std::span<const std::byte> bytes);
#else
inline void start() {}
inline void pose(const pure::RecordedPoseFrame&) {}
inline void appearance(unsigned, std::span<const std::byte>) {}
inline void binding(pure::PoseFrameKey, std::span<const unsigned>) {}
inline void schema(unsigned, unsigned, unsigned, unsigned, std::span<const std::byte>) {}
#endif
} // namespace self_recall::corpus
