// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "MotherBase.hpp"

#include <cmath>
#include <cstdio>

namespace {

enum class FieldMode {
    NoWater,
    Shallow,
    ShallowThenDeep,
    Deep,
    Narrow,
    Uneven,
    Unavailable,
    ShallowThenUnavailable,
};

struct Field {
    FieldMode mode;
};

int gFailures = 0;

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                              \
            gFailures++;                                                                            \
        }                                                                                           \
    } while (false)

bool fakeWater(void* context, float x, float z, motherbase::WaterSample* out) {
    Field* field = static_cast<Field*>(context);
    *out = {};
    if (field->mode == FieldMode::Unavailable) { return false; }
    if (field->mode == FieldMode::ShallowThenUnavailable && z > 4.0f) { return false; }
    if (field->mode == FieldMode::NoWater) {
        out->nativeCode = 0;
        return true;
    }

    bool hasWater = true;
    float depth = 5.0f;
    float surfaceY = 100.0f;
    if (field->mode == FieldMode::Shallow || field->mode == FieldMode::ShallowThenUnavailable) {
        depth = 2.0f;
    }
    if (field->mode == FieldMode::ShallowThenDeep) { depth = z < 10.0f ? 2.0f : 5.0f; }
    if (field->mode == FieldMode::Narrow) { hasWater = std::fabs(x) <= 9.0f; }
    if (field->mode == FieldMode::Uneven && x > 0.0f) { surfaceY = 101.0f; }

    out->nativeCode = hasWater ? 1 : 0;
    out->hasWater = hasWater;
    if (hasWater) {
        out->surfaceY = surfaceY;
        out->bottomY = surfaceY - depth;
        out->depth = depth;
    }
    return true;
}

void driveWater(motherbase::Placement& placement, Field& field) {
    for (int i = 0; i < 1000; i++) {
        const motherbase::Phase phase = placement.phase();
        if (phase != motherbase::Phase::ScanAhead
            && phase != motherbase::Phase::ValidateFootprint) {
            return;
        }
        placement.advanceWater(&fakeWater, &field, 8);
    }
    CHECK(false && "water solver did not terminate");
}

motherbase::Placement beginAndDrive(FieldMode mode, float linkY = 100.0f) {
    motherbase::Placement placement;
    Field field{mode};
    placement.begin({0.0f, linkY, 0.0f}, 0.0f, 1.0f);
    driveWater(placement, field);
    return placement;
}

void acceptVisibleIntent(motherbase::Placement& placement) {
    CHECK(placement.phase() == motherbase::Phase::ValidateIntent);
    CHECK(placement.wantsIntentRay());
    placement.acceptIntentResult(false, false, 0.0f, 0xFFFFFFFFu);
}

void driveRemainingWater(motherbase::Placement& placement, FieldMode mode) {
    Field field{mode};
    driveWater(placement, field);
}

void testNoWaterIsOrdinaryEat() {
    motherbase::Placement placement = beginAndDrive(FieldMode::NoWater);
    CHECK(placement.phase() == motherbase::Phase::Rejected);
    CHECK(placement.rejectReason() == motherbase::RejectReason::NoWaterAhead);
    CHECK(!placement.shouldRefund());
    CHECK(!placement.sawWater());
}

void testWaterFailuresRefund() {
    motherbase::Placement shallow = beginAndDrive(FieldMode::Shallow);
    acceptVisibleIntent(shallow);
    CHECK(shallow.rejectReason() == motherbase::RejectReason::WaterTooShallow);
    CHECK(shallow.shouldRefund());
    CHECK(shallow.sawWater());
    CHECK(shallow.visibleWaterIntent());

    motherbase::Placement narrow = beginAndDrive(FieldMode::Narrow);
    acceptVisibleIntent(narrow);
    driveRemainingWater(narrow, FieldMode::Narrow);
    CHECK(narrow.rejectReason() == motherbase::RejectReason::FootprintNotWater);
    CHECK(narrow.shouldRefund());
    CHECK(narrow.candidateAttempts() > 1);

    motherbase::Placement uneven = beginAndDrive(FieldMode::Uneven);
    acceptVisibleIntent(uneven);
    driveRemainingWater(uneven, FieldMode::Uneven);
    CHECK(uneven.rejectReason() == motherbase::RejectReason::SurfaceUneven);
    CHECK(uneven.shouldRefund());

    motherbase::Placement unavailable = beginAndDrive(FieldMode::Unavailable);
    CHECK(unavailable.rejectReason() == motherbase::RejectReason::QueryUnavailable);
    CHECK(!unavailable.shouldRefund());

    motherbase::Placement visibleThenUnavailable = beginAndDrive(FieldMode::ShallowThenUnavailable);
    acceptVisibleIntent(visibleThenUnavailable);
    CHECK(visibleThenUnavailable.rejectReason() == motherbase::RejectReason::QueryUnavailable);
    CHECK(visibleThenUnavailable.shouldRefund());
}

void testCastOutAndFullClearance() {
    motherbase::Placement placement = beginAndDrive(FieldMode::ShallowThenDeep);
    CHECK(placement.phase() == motherbase::Phase::ValidateIntent);
    const motherbase::IntentRay sight = placement.intentRay();
    CHECK(std::fabs(sight.from.x) < 0.001f);
    CHECK(std::fabs(sight.from.y - 101.55f) < 0.001f);
    CHECK(std::fabs(sight.from.z - 0.75f) < 0.001f);
    CHECK(std::fabs(sight.to.x) < 0.001f);
    CHECK(std::fabs(sight.to.y - 100.30f) < 0.001f);
    CHECK(std::fabs(sight.to.z - 17.0f) < 0.001f);
    acceptVisibleIntent(placement);
    driveRemainingWater(placement, FieldMode::ShallowThenDeep);
    CHECK(placement.phase() == motherbase::Phase::ValidateClearance);
    CHECK(std::fabs(placement.readySite().anchorDistanceM - 17.0f) < 0.001f);
    CHECK(std::fabs(placement.readySite().anchor.z - 17.0f) < 0.001f);
    CHECK(std::fabs(placement.readySite().anchor.y - 100.30f) < 0.001f);
    CHECK(std::fabs(placement.readySite().nx) < 0.001f);
    CHECK(std::fabs(placement.readySite().nz - 1.0f) < 0.001f);

    const motherbase::ClearanceRay first = placement.clearanceRay();
    CHECK(std::fabs(first.from.x + 22.0f) < 0.001f);
    CHECK(std::fabs(first.from.z - 11.0f) < 0.001f);
    while (placement.phase() == motherbase::Phase::ValidateClearance) {
        CHECK(placement.wantsClearanceRay());
        placement.acceptClearanceResult(false, false, 0.0f, 0xFFFFFFFFu);
    }
    CHECK(placement.phase() == motherbase::Phase::Ready);
    CHECK(placement.clearanceSamplesTested()
          == static_cast<u16>(motherbase::kMvpConfig.gridColumns * motherbase::kMvpConfig.gridRows));
}

void testObstructionAndFacingGuards() {
    motherbase::Placement blocked = beginAndDrive(FieldMode::Deep);
    acceptVisibleIntent(blocked);
    driveRemainingWater(blocked, FieldMode::Deep);
    CHECK(blocked.phase() == motherbase::Phase::ValidateClearance);
    blocked.acceptClearanceResult(false, true, 99.5f, 0x1234u);
    CHECK(blocked.rejectReason() == motherbase::RejectReason::Obstructed);
    CHECK(blocked.obstructionBodyId() == 0x1234u);
    CHECK(blocked.shouldRefund());

    motherbase::Placement invalid;
    invalid.begin({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
    CHECK(invalid.rejectReason() == motherbase::RejectReason::InvalidFacing);
    CHECK(!invalid.shouldRefund());
}

void testVisibilityAndHeightGuards() {
    motherbase::Placement hidden = beginAndDrive(FieldMode::Deep);
    CHECK(hidden.phase() == motherbase::Phase::ValidateIntent);
    hidden.acceptIntentResult(false, true, 101.0f, 0x5678u);
    CHECK(hidden.rejectReason() == motherbase::RejectReason::WaterNotVisible);
    CHECK(!hidden.visibleWaterIntent());
    CHECK(!hidden.shouldRefund());
    CHECK(hidden.intentBodyId() == 0x5678u);

    motherbase::Placement sightTimeout = beginAndDrive(FieldMode::Deep);
    sightTimeout.acceptIntentResult(true, false, 0.0f, 0xFFFFFFFFu);
    CHECK(sightTimeout.rejectReason() == motherbase::RejectReason::VisibilityTimedOut);
    CHECK(!sightTimeout.shouldRefund());

    motherbase::Placement farBelow = beginAndDrive(FieldMode::Deep, 168.4f);
    CHECK(farBelow.phase() == motherbase::Phase::Rejected);
    CHECK(farBelow.rejectReason() == motherbase::RejectReason::WaterOutsideHeightLimit);
    CHECK(farBelow.intentSurfaceDeltaM() > motherbase::kMvpConfig.maxSurfaceBelowLinkM);
    CHECK(!farBelow.wantsIntentRay());
    CHECK(!farBelow.shouldRefund());

    motherbase::Placement elevated = beginAndDrive(FieldMode::Deep, 133.5f);
    CHECK(elevated.phase() == motherbase::Phase::ValidateIntent);
    CHECK(elevated.intentSurfaceDeltaM() < motherbase::kMvpConfig.maxSurfaceBelowLinkM);
    CHECK(elevated.wantsIntentRay());
    acceptVisibleIntent(elevated);
    driveRemainingWater(elevated, FieldMode::Deep);
    CHECK(elevated.phase() == motherbase::Phase::ValidateClearance);

    motherbase::Placement waterFarAbove = beginAndDrive(FieldMode::Deep, 80.0f);
    CHECK(waterFarAbove.phase() == motherbase::Phase::Rejected);
    CHECK(waterFarAbove.rejectReason() == motherbase::RejectReason::WaterOutsideHeightLimit);
    CHECK(waterFarAbove.intentSurfaceDeltaM() > motherbase::kMvpConfig.maxSurfaceAboveLinkM);
    CHECK(!waterFarAbove.wantsIntentRay());
}

void testBoundaryAndBudgetGuards() {
    Field deep{FieldMode::Deep};

    motherbase::Placement noBudget;
    noBudget.begin({0.0f, 100.0f, 0.0f}, 0.0f, 1.0f);
    noBudget.advanceWater(&fakeWater, &deep, 0);
    CHECK(noBudget.phase() == motherbase::Phase::ScanAhead);
    CHECK(noBudget.waterSamplesTested() == 0);
    noBudget.advanceWater(nullptr, &deep, 8);
    CHECK(noBudget.phase() == motherbase::Phase::ScanAhead);
    CHECK(noBudget.waterSamplesTested() == 0);

    motherbase::Placement exactBelow = beginAndDrive(FieldMode::Deep, 140.0f);
    CHECK(exactBelow.phase() == motherbase::Phase::ValidateIntent);
    motherbase::Placement beyondBelow = beginAndDrive(FieldMode::Deep, 140.01f);
    CHECK(beyondBelow.rejectReason() == motherbase::RejectReason::WaterOutsideHeightLimit);

    motherbase::Placement exactAbove = beginAndDrive(FieldMode::Deep, 88.0f);
    CHECK(exactAbove.phase() == motherbase::Phase::ValidateIntent);
    motherbase::Placement beyondAbove = beginAndDrive(FieldMode::Deep, 87.99f);
    CHECK(beyondAbove.rejectReason() == motherbase::RejectReason::WaterOutsideHeightLimit);
}

void testPhaseAndResetGuards() {
    motherbase::Placement idle;
    idle.acceptIntentResult(false, true, 1.0f, 10u);
    idle.acceptClearanceResult(false, true, 1.0f, 11u);
    CHECK(idle.phase() == motherbase::Phase::Idle);
    CHECK(idle.rejectReason() == motherbase::RejectReason::None);

    motherbase::Placement timedOut = beginAndDrive(FieldMode::Deep);
    acceptVisibleIntent(timedOut);
    driveRemainingWater(timedOut, FieldMode::Deep);
    CHECK(timedOut.phase() == motherbase::Phase::ValidateClearance);
    timedOut.acceptClearanceResult(true, false, 0.0f, 0xFFFFFFFFu);
    CHECK(timedOut.rejectReason() == motherbase::RejectReason::ClearanceTimedOut);
    CHECK(timedOut.shouldRefund());

    timedOut.reset();
    CHECK(timedOut.phase() == motherbase::Phase::Idle);
    CHECK(timedOut.rejectReason() == motherbase::RejectReason::None);
    CHECK(!timedOut.shouldRefund());
    CHECK(timedOut.waterSamplesTested() == 0);
    CHECK(timedOut.clearanceSamplesTested() == 0);
}

void testFacingNormalization() {
    motherbase::Placement diagonal;
    Field deep{FieldMode::Deep};
    diagonal.begin({10.0f, 100.0f, 20.0f}, 3.0f, 4.0f);
    driveWater(diagonal, deep);
    CHECK(diagonal.phase() == motherbase::Phase::ValidateIntent);
    const motherbase::IntentRay ray = diagonal.intentRay();
    CHECK(std::fabs(ray.from.x - 10.45f) < 0.001f);
    CHECK(std::fabs(ray.from.z - 20.60f) < 0.001f);
    CHECK(ray.to.x > ray.from.x);
    CHECK(ray.to.z > ray.from.z);
}

void testPendingTriggerOwnership() {
    CHECK(!motherbase::shouldRefundAdditionalTrigger(false, true));
    CHECK(!motherbase::shouldRefundAdditionalTrigger(true, false));
    CHECK(motherbase::shouldRefundAdditionalTrigger(true, true));
    CHECK(!motherbase::shouldRefundPlacementFailure(false, false));
    CHECK(motherbase::shouldRefundPlacementFailure(true, false));
    CHECK(motherbase::shouldRefundPlacementFailure(false, true));
}

void testLedgerCompatibilityBits() {
    CHECK(motherbase::ledgerSiteFlags(false) == 1u);
    CHECK(motherbase::ledgerSiteFlags(true) == 3u);
    CHECK(!motherbase::ledgerSiteIsWater(1u));
    CHECK(motherbase::ledgerSiteIsWater(motherbase::ledgerSiteFlags(true)));
    CHECK(motherbase::ledgerSiteIsWater(2u));
    CHECK(motherbase::ledgerSiteIsWater(0xFFFFFFFFu));
}

} // namespace

int main() {
    testNoWaterIsOrdinaryEat();
    testWaterFailuresRefund();
    testCastOutAndFullClearance();
    testObstructionAndFacingGuards();
    testVisibilityAndHeightGuards();
    testBoundaryAndBudgetGuards();
    testPhaseAndResetGuards();
    testFacingNormalization();
    testPendingTriggerOwnership();
    testLedgerCompatibilityBits();
    if (gFailures == 0) {
        std::puts("motherbase solver tests: PASS");
        return 0;
    }
    std::printf("motherbase solver tests: %d failure(s)\n", gFailures);
    return 1;
}
