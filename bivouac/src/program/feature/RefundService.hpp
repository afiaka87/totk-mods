// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include "../RefundPolicy.hpp"

#include <cstdint>

namespace bivouac::feature {

inline constexpr int kRefundQueueCapacity = 4;
inline constexpr int kRefundItemNameCapacity = 64;

enum class RefundScheduleStatus : std::uint8_t {
    Scheduled = 0,
    NotRefundable,
    QueueFull,
};

struct RefundScheduleResult {
    RefundScheduleStatus status = RefundScheduleStatus::NotRefundable;
    int queueIndex = -1;

    constexpr explicit operator bool() const {
        return status == RefundScheduleStatus::Scheduled;
    }
};

struct DueRefund {
    bool available = false;
    int queueIndex = -1;
    std::uint32_t gameDataIndex = 0;
    char itemName[kRefundItemNameCapacity] = {};
};

// Item-hook producer, gameplay-tick consumer: `pending` is published last and cleared after copy.
class RefundService {
public:
    RefundScheduleResult schedule(const char* itemName,
                                  std::uint32_t gameDataIndex,
                                  int currentTick,
                                  int delayTicks);

    DueRefund takeDue(int currentTick);

    bool hasPending() const;
    int pendingCount() const;

private:
    volatile std::uint32_t mPending[kRefundQueueCapacity] = {};
    int mDueTick[kRefundQueueCapacity] = {};
    std::uint32_t mGameDataIndex[kRefundQueueCapacity] = {};
    char mItemName[kRefundQueueCapacity][kRefundItemNameCapacity] = {};
};

} // namespace bivouac::feature
