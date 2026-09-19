// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "feature/RefundService.hpp"

#include <cstdio>
#include <cstring>

namespace {

int gFailures = 0;

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                              \
            ++gFailures;                                                                            \
        }                                                                                           \
    } while (false)

void testPublicationAndDueTime() {
    bivouac::feature::RefundService refunds;
    const auto scheduled = refunds.schedule("Item_Fruit_P", 12, 100, 12);
    CHECK(scheduled);
    CHECK(scheduled.queueIndex == 0);
    CHECK(refunds.pendingCount() == 1);
    CHECK(!refunds.takeDue(111).available);

    const auto due = refunds.takeDue(112);
    CHECK(due.available);
    CHECK(due.queueIndex == 0);
    CHECK(due.gameDataIndex == 12);
    CHECK(std::strcmp(due.itemName, "Item_Fruit_P") == 0);
    CHECK(!refunds.hasPending());
}

void testIndependentSlotsAndQueueOrder() {
    bivouac::feature::RefundService refunds;
    CHECK(refunds.schedule("A", 1, 0, 20).queueIndex == 0);
    CHECK(refunds.schedule("B", 2, 0, 10).queueIndex == 1);
    CHECK(refunds.schedule("C", 3, 0, 10).queueIndex == 2);
    CHECK(refunds.schedule("D", 4, 0, 10).queueIndex == 3);
    CHECK(refunds.pendingCount() == 4);

    const auto full = refunds.schedule("E", 5, 0, 10);
    CHECK(full.status == bivouac::feature::RefundScheduleStatus::QueueFull);

    // The scan chooses the first slot that is currently due.
    const auto first = refunds.takeDue(10);
    CHECK(first.queueIndex == 1);
    const auto second = refunds.takeDue(10);
    CHECK(second.queueIndex == 2);
    const auto third = refunds.takeDue(20);
    CHECK(third.queueIndex == 0);
    CHECK(refunds.schedule("E", 5, 20, 0).queueIndex == 0);
}

void testNonRefundableAndBoundedName() {
    bivouac::feature::RefundService refunds;
    CHECK(refunds.schedule(nullptr, 1, 0, 0).status
          == bivouac::feature::RefundScheduleStatus::NotRefundable);
    CHECK(refunds.schedule("", 1, 0, 0).status
          == bivouac::feature::RefundScheduleStatus::NotRefundable);

    char longName[96] = {};
    for (int index = 0; index < 95; ++index) {
        longName[index] = 'X';
    }
    CHECK(refunds.schedule(longName, 9, 0, 0));
    const auto due = refunds.takeDue(0);
    CHECK(due.available);
    CHECK(due.itemName[62] == 'X');
    CHECK(due.itemName[63] == '\0');
}

} // namespace

int main() {
    testPublicationAndDueTime();
    testIndependentSlotsAndQueueOrder();
    testNonRefundableAndBoundedName();
    if (gFailures == 0) {
        std::puts("refund service tests: PASS");
        return 0;
    }
    std::printf("refund service tests: %d failure(s)\n", gFailures);
    return 1;
}
