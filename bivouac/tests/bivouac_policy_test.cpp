// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "BivouacPolicy.hpp"
#include "MapPolicy.hpp"
#include "MenuPolicy.hpp"
#include "PersistencePolicy.hpp"
#include "PlacementPolicy.hpp"
#include "RefundPolicy.hpp"

#include <cstdio>

namespace {

int gFailures = 0;

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                              \
            ++gFailures;                                                                            \
        }                                                                                           \
    } while (false)

void testExactTierItems() {
    using namespace bivouac::policy;
    const TierMatch camp = tierForItem("Item_Mushroom_N");
    const TierMatch bivy = tierForItem("Item_Mushroom_F");
    const TierMatch base = tierForItem("Item_PlantGet_C");
    CHECK(camp.matched && camp.tier == CampTier::CAMP);
    CHECK(bivy.matched && bivy.tier == CampTier::BIVY);
    CHECK(base.matched && base.tier == CampTier::BASE);
    CHECK(!tierForItem("Item_Fruit_B").matched);
    CHECK(!tierForItem("Item_Fruit_P").matched);
    CHECK(!tierForItem("item_mushroom_n").matched);
    CHECK(!tierForItem(nullptr).matched);
}

void testRecentClimbBoundaries() {
    using bivouac::policy::climbIsRecent;
    CHECK(climbIsRecent(100, 67, -1000, 33, 165));
    CHECK(!climbIsRecent(100, 66, -1000, 33, 165));
    CHECK(climbIsRecent(1000, -1000, 835, 33, 165));
    CHECK(!climbIsRecent(1000, -1000, 834, 33, 165));
    CHECK(!climbIsRecent(100, 101, 101, 33, 165));
    using bivouac::policy::heartbeatClimbing;
    CHECK(heartbeatClimbing(100, 100, 4));
    CHECK(heartbeatClimbing(104, 100, 4));
    CHECK(!heartbeatClimbing(105, 100, 4));
    CHECK(!heartbeatClimbing(99, 100, 4));
    CHECK(!heartbeatClimbing(100, -1000, 4));
}

void testComposedClimbObservationOverridesOnlyWhenSupplied() {
    using bivouac::policy::ClimbObservation;
    using bivouac::policy::resolveClimbing;
    CHECK(resolveClimbing(ClimbObservation::LocalSensor, true));
    CHECK(!resolveClimbing(ClimbObservation::LocalSensor, false));
    CHECK(!resolveClimbing(ClimbObservation::NotClimbing, true));
    CHECK(resolveClimbing(ClimbObservation::Climbing, false));
}

void testPlacementPolicy() {
    using namespace bivouac::placement;
    CHECK(initialLane(true) == Lane::Cliff);
    CHECK(initialLane(false) == Lane::MotherBase);
    CHECK(timeoutTicks(Lane::Cliff, 160, 330) == 160);
    CHECK(timeoutTicks(Lane::MotherBase, 160, 330) == 330);
    CHECK(!timedOut(160, 0, Lane::Cliff, 160, 330));
    CHECK(timedOut(161, 0, Lane::Cliff, 160, 330));
    CHECK(!timedOut(330, 0, Lane::MotherBase, 160, 330));
    CHECK(timedOut(331, 0, Lane::MotherBase, 160, 330));
    CHECK(!timedOut(9, 10, Lane::Cliff, 160, 330));
    CHECK(!wallRetriesExhausted(2, 3));
    CHECK(wallRetriesExhausted(3, 3));
    CHECK(clampSeat(3.0f, 2.5f) == 2.5f);
    CHECK(clampSeat(2.0f, 2.5f) == 2.0f);
    CHECK(boundedGap(1.0f, 2.0f, 5.4f) == 0.0f);
    CHECK(boundedGap(6.0f, 0.0f, 5.4f) == 5.4f);
    CHECK(boundedGap(4.0f, 1.0f, 5.4f) == 3.0f);
    CHECK(minimumSeparation(Lane::Cliff, 20.0f, 50.0f) == 20.0f);
    CHECK(minimumSeparation(Lane::MotherBase, 20.0f, 50.0f) == 50.0f);
}

void testPersistencePolicy() {
    using namespace bivouac::persistence;
    const uint32_t packed = packSiteMeta(2, 1.24f, 2.50f, 7);
    const PackedSiteMeta decoded = unpackSiteMeta(packed, 2);
    CHECK(decoded.tier == 2);
    CHECK(decoded.floorGap > 1.239f && decoded.floorGap < 1.241f);
    CHECK(decoded.roofGap > 2.499f && decoded.roofGap < 2.501f);
    CHECK(decoded.stampSlot == 7);

    CHECK(unpackSiteMeta(packSiteMeta(9, 0, 0, kStampSlotNone), 2).tier == 0);
    CHECK(unpackSiteMeta(packSiteMeta(0, 0, 0, 254), 2).stampSlot == 254);
    CHECK(unpackSiteMeta(packSiteMeta(0, 0, 0, 255), 2).stampSlot == kStampSlotNone);
    CHECK(payloadShapeMatches(160, 32, 32, 4, 64));
    CHECK(!payloadShapeMatches(161, 32, 32, 4, 64));
    CHECK(!payloadShapeMatches(32, 32, 32, 65, 64));
    CHECK(positionsOverlap(0, 0, 0, 19.99f, 0, 0, 20.0f));
    CHECK(!positionsOverlap(0, 0, 0, 20.0f, 0, 0, 20.0f));
}

void testRefundPolicy() {
    using namespace bivouac::refund;
    const uint32_t pending[4] = {1, 0, 1, 0};
    const int due[4] = {8, 0, 12, 0};
    CHECK(firstFreeSlot(pending, 4) == 1);
    CHECK(firstFreeSlot(static_cast<const uint32_t*>(nullptr), 4) == -1);
    CHECK(firstDueSlot(pending, due, 4, 7) == -1);
    CHECK(firstDueSlot(pending, due, 4, 8) == 0);
    CHECK(firstDueSlot(pending, due, 4, 20) == 0);
    CHECK(decideSlotName("", "Item_Fruit_P") == SlotNameDecision::RestoreEmptiedName);
    CHECK(decideSlotName("Item_Fruit_P", "Item_Fruit_P")
          == SlotNameDecision::AddToMatchingSlot);
    CHECK(decideSlotName("Item_Fruit_A", "Item_Fruit_P")
          == SlotNameDecision::RefuseDifferentItem);
    CHECK(decideSlotName(nullptr, "Item_Fruit_P")
          == SlotNameDecision::RefuseDifferentItem);
}

void testMenuPolicy() {
    using namespace bivouac::menu;
    Step step = update(0, false, 4);
    CHECK(step.opened);
    CHECK(!step.closed);
    CHECK(step.starvedTicks == 1);

    step = update(3, true, 4);
    CHECK(!step.closed);
    CHECK(step.starvedTicks == 0);

    step = update(4, true, 4);
    CHECK(step.closed);
    CHECK(step.starvedTicks == 0);

    step = update(kStarvationSaturation, false, 4);
    CHECK(step.starvedTicks == kStarvationSaturation);
    CHECK(fullScreenMenuOpen(4, 4));
    CHECK(!fullScreenMenuOpen(3, 4));
}

void testMapPolicy() {
    using namespace bivouac::map;
    CHECK(ownsStampType(10, 10, 11));
    CHECK(ownsStampType(11, 10, 11));
    CHECK(!ownsStampType(12, 10, 11));
    struct Probe { bool active; unsigned short stampSlot; };
    const Probe probes[4] = {{true, 3}, {false, 5}, {true, 5}, {true, 0xFFFFu}};
    CHECK(siteOwningStampSlot(probes, 4, 3) == 0);
    CHECK(siteOwningStampSlot(probes, 4, 5) == 2);
    CHECK(siteOwningStampSlot(probes, 4, 4) == -1);
    CHECK(siteOwningStampSlot(probes, 4, 0xFFFFu) == 3);
    CHECK(siteOwningStampSlot(probes, 0, 3) == -1);
}

} // namespace

int main() {
    testExactTierItems();
    testRecentClimbBoundaries();
    testComposedClimbObservationOverridesOnlyWhenSupplied();
    testPlacementPolicy();
    testPersistencePolicy();
    testRefundPolicy();
    testMenuPolicy();
    testMapPolicy();
    if (gFailures == 0) {
        std::puts("bivouac policy tests: PASS");
        return 0;
    }
    std::printf("bivouac policy tests: %d failure(s)\n", gFailures);
    return 1;
}
