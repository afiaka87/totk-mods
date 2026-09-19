// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "RaycastService.hpp"

#include <cstring>

namespace bivouac::engine {
namespace {

inline constexpr int kHitOffset = 0x20;
inline constexpr int kPositionOffset = 0x24;
inline constexpr int kNormalOffset = 0x30;
inline constexpr int kDistanceOffset = 0x40;
inline constexpr int kBodyIdOffset = 0x120;

template <typename Value>
Value readValue(const unsigned char* bytes, int offset) {
    Value value{};
    std::memcpy(&value, bytes + offset, sizeof(value));
    return value;
}

} // namespace

bool RaycastService::request(const float from[3], const float to[3],
                             std::uint32_t mask, int currentTick) {
    if (mCastBusy || from == nullptr || to == nullptr) {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        mRequestFrom[axis] = from[axis];
        mRequestTo[axis] = to[axis];
    }
    mRequestMask = mask;
    mResultReady = 0;
    mRequestPending = 1; // Publication is intentionally last.
    mCastBusy = true;
    mCastStartTick = currentTick;
    return true;
}

RaycastResult RaycastService::poll(int currentTick, int timeoutTicks) {
    RaycastResult result{};
    if (mResultReady == 1) {
        result.resolved = true;
        result.hit = readValue<std::uint32_t>(mResultObject, kHitOffset);
        result.distance = readValue<float>(mResultObject, kDistanceOffset);
        for (int axis = 0; axis < 3; ++axis) {
            result.position[axis] =
                readValue<float>(mResultObject, kPositionOffset + axis * 4);
            result.normal[axis] =
                readValue<float>(mResultObject, kNormalOffset + axis * 4);
        }
        result.bodyId = readValue<std::uint32_t>(mResultObject, kBodyIdOffset);
    } else if (mWorkerAliveThisTick
               && currentTick - mCastStartTick > timeoutTicks) {
        result.resolved = true;
        result.timedOut = true;
    }
    if (result.resolved) {
        mCastBusy = false;
    }
    return result;
}

void RaycastService::cancel() {
    mRequestPending = 0;
    mResultReady = 0;
    mCastBusy = false;
}

void RaycastService::noteWorkerCall() {
    mWorkerSequence = mWorkerSequence + 1;
}

bool RaycastService::observeWorkerForTick() {
    mWorkerAliveThisTick = mWorkerSequence != mObservedWorkerSequence;
    mObservedWorkerSequence = mWorkerSequence;
    return mWorkerAliveThisTick;
}

void RaycastService::prepareWorkerObject(const void* source) {
    std::memcpy(mResultObject, source, sizeof(mResultObject));
}

void RaycastService::publishWorkerResult() {
    mResultReady = 1;
    mRequestPending = 0;
}

} // namespace bivouac::engine
