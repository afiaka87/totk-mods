// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "feature/CampGeometry.hpp"

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

bool near(float left, float right) {
    const float difference = left - right;
    return difference > -0.0001f && difference < 0.0001f;
}

void testOrdinaryPropTransform() {
    bivouac::feature::SiteGeometry site{};
    site.anchor = {10.0f, 20.0f, 30.0f};
    site.normalX = 1.0f;
    site.normalZ = 0.0f;

    bivouac::feature::PropDefinition prop{};
    prop.lateralOffset = 2.0f;
    prop.outwardOffset = 3.0f;
    prop.lift = 4.0f;
    prop.yawSin = 0.0f;
    prop.yawCos = 1.0f;

    const auto transform = bivouac::feature::worldTransform(
        site, prop, {});
    CHECK(near(transform.position.x, 13.0f));
    CHECK(near(transform.position.y, 24.0f));
    CHECK(near(transform.position.z, 28.0f));
    CHECK(near(transform.column0.x, 1.0f));
    CHECK(near(transform.column2.z, 1.0f));
}

void testBackerShiftAndCap() {
    bivouac::feature::SiteGeometry site{};
    site.floorGap = 3.0f;
    site.roofGap = 8.0f;
    const bivouac::feature::BackerTuning tuning{
        0.2f, 0.3f, 6.0f, 4.0f};

    bivouac::feature::PropDefinition floor{};
    floor.backerLevel = 1;
    CHECK(near(bivouac::feature::backerShift(site, floor, tuning), 3.5f));

    bivouac::feature::PropDefinition roofBridge{};
    roofBridge.backerLevel = 2;
    roofBridge.backerTile = 1;
    CHECK(near(
        bivouac::feature::backerShift(site, roofBridge, tuning), 2.0f));
}

void testRightHandedYaw() {
    bivouac::feature::SiteGeometry site{};
    site.normalX = 0.0f;
    site.normalZ = 1.0f;
    bivouac::feature::PropDefinition prop{};
    prop.yawSin = 1.0f;
    prop.yawCos = 0.0f;

    const auto transform = bivouac::feature::worldTransform(site, prop, {});
    CHECK(near(transform.column0.x, 1.0f));
    CHECK(near(transform.column0.z, 0.0f));
    CHECK(near(transform.column2.x, 0.0f));
    CHECK(near(transform.column2.z, 1.0f));
}

} // namespace

int main() {
    testOrdinaryPropTransform();
    testBackerShiftAndCap();
    testRightHandedYaw();
    if (gFailures == 0) {
        std::puts("camp geometry tests: PASS");
        return 0;
    }
    std::printf("camp geometry tests: %d failure(s)\n", gFailures);
    return 1;
}
