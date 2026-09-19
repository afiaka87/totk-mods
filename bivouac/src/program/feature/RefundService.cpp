// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "RefundService.hpp"

namespace bivouac::feature {
namespace {

void copyItemName(char* destination, const char* source) {
    int index = 0;
    for (; index < kRefundItemNameCapacity - 1 && source[index] != '\0'; ++index) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
}

} // namespace

RefundScheduleResult RefundService::schedule(const char* itemName,
                                             std::uint32_t gameDataIndex,
                                             int currentTick,
                                             int delayTicks) {
    if (itemName == nullptr || itemName[0] == '\0') {
        return {RefundScheduleStatus::NotRefundable};
    }
    const int queueIndex =
        refund::firstFreeSlot(mPending, kRefundQueueCapacity);
    if (queueIndex < 0) {
        return {RefundScheduleStatus::QueueFull};
    }

    copyItemName(mItemName[queueIndex], itemName);
    mGameDataIndex[queueIndex] = gameDataIndex;
    mDueTick[queueIndex] = currentTick + delayTicks;
    mPending[queueIndex] = 1; // Publication is intentionally last.
    return {RefundScheduleStatus::Scheduled, queueIndex};
}

DueRefund RefundService::takeDue(int currentTick) {
    const int queueIndex =
        refund::firstDueSlot(mPending, mDueTick, kRefundQueueCapacity, currentTick);
    if (queueIndex < 0) {
        return {};
    }

    DueRefund due{};
    due.available = true;
    due.queueIndex = queueIndex;
    due.gameDataIndex = mGameDataIndex[queueIndex];
    copyItemName(due.itemName, mItemName[queueIndex]);
    mPending[queueIndex] = 0;
    return due;
}

bool RefundService::hasPending() const {
    return pendingCount() != 0;
}

int RefundService::pendingCount() const {
    int count = 0;
    for (int index = 0; index < kRefundQueueCapacity; ++index) {
        if (mPending[index] != 0) {
            ++count;
        }
    }
    return count;
}

} // namespace bivouac::feature
