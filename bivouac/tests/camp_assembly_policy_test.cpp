// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "feature/CampAssemblyPolicy.hpp"

#include <cstdio>

namespace {

int gFailures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);        \
            ++gFailures;                                                     \
        }                                                                    \
    } while (false)

bivouac::feature::SpawnFacts readyFacts() {
    using namespace bivouac::feature;
    return {
        0b001,
        0,
        false,
        0,
        0.0f,
        0.25f,
        false,
        true,
        AssemblySlotState::Unspawned,
        0,
        5,
        100,
        0,
        40.0f,
        70.0f,
        false,
        true,
    };
}

void testAdmission() {
    using namespace bivouac::feature;
    SpawnFacts facts = readyFacts();
    CHECK(shouldSpawn(facts));

    facts.siteTier = 1;
    CHECK(!shouldSpawn(facts));
    facts = readyFacts();
    facts.waterSite = true;
    facts.backerLevel = 1;
    CHECK(!shouldSpawn(facts));
    facts = readyFacts();
    facts.backerLevel = 1;
    facts.backerShift = 0.24f;
    CHECK(!shouldSpawn(facts));
    facts.backerShift = 0.25f;
    CHECK(shouldSpawn(facts));
    facts = readyFacts();
    facts.hasDependency = true;
    facts.dependencyAlive = false;
    CHECK(!shouldSpawn(facts));
    facts = readyFacts();
    facts.slotState = AssemblySlotState::Alive;
    CHECK(!shouldSpawn(facts));
    facts = readyFacts();
    facts.failures = 5;
    CHECK(!shouldSpawn(facts));
    facts = readyFacts();
    facts.retryAtTick = 101;
    CHECK(!shouldSpawn(facts));
    facts = readyFacts();
    facts.distanceToPlayer = 70.01f;
    CHECK(!shouldSpawn(facts));
    facts = readyFacts();
    facts.sentinelReady = false;
    CHECK(!shouldSpawn(facts));
    facts.isSentinel = true;
    CHECK(shouldSpawn(facts));
}

void testRetryBackoff() {
    using bivouac::feature::retryAtTick;
    CHECK(retryAtTick(100, 22, 4, 5) == 122);
    CHECK(retryAtTick(100, 22, 5, 5) == 320);
}

} // namespace

int main() {
    testAdmission();
    testRetryBackoff();
    if (gFailures == 0) {
        std::puts("camp assembly policy tests: PASS");
        return 0;
    }
    std::printf(
        "camp assembly policy tests: %d failure(s)\n", gFailures);
    return 1;
}
