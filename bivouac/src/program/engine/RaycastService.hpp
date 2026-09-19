// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace bivouac::engine {

struct RaycastResult {
    bool resolved = false;
    bool timedOut = false;
    std::uint32_t hit = 0;
    float distance = 0.0f;
    float position[3] = {};
    float normal[3] = {};
    std::uint32_t bodyId = 0;
};

class RaycastService {
public:
    bool request(const float from[3], const float to[3],
                 std::uint32_t mask, int currentTick);

    RaycastResult poll(int currentTick, int timeoutTicks);
    void cancel();

    void noteWorkerCall();
    bool observeWorkerForTick();
    bool workerAliveThisTick() const { return mWorkerAliveThisTick; }

    bool requestPending() const { return mRequestPending != 0; }
    bool castBusy() const { return mCastBusy; }
    const float* requestFrom() const { return mRequestFrom; }
    const float* requestTo() const { return mRequestTo; }
    std::uint32_t requestMask() const { return mRequestMask; }

    void prepareWorkerObject(const void* source);
    void* workerObject() { return mResultObject; }
    void publishWorkerResult();

private:
    volatile std::uint32_t mRequestPending = 0;
    std::uint32_t mRequestMask = 0;
    float mRequestFrom[3] = {};
    float mRequestTo[3] = {};
    volatile std::uint32_t mResultReady = 0;
    alignas(16) unsigned char mResultObject[0x200] = {};
    bool mCastBusy = false;
    int mCastStartTick = 0;

    volatile std::uint32_t mWorkerSequence = 0;
    std::uint32_t mObservedWorkerSequence = 0;
    bool mWorkerAliveThisTick = false;
};

} // namespace bivouac::engine
