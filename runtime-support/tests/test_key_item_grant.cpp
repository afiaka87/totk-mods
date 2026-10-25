// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "totk/engine/KeyItemGrant.hpp"

#include <array>
#include <cstring>

namespace {
using namespace totk::engine;

struct Calls {
    int resolved = 0;
    int added = 0;
    std::uint8_t resolveResult = 1;
    std::uint32_t category = 8;
    std::uint64_t addResult = 37;
    const char* actor = nullptr;
    std::array<std::uint64_t, 14> arguments{};
} calls;

std::uint8_t resolve(std::uint32_t* category, const char** name) {
    ++calls.resolved;
    calls.actor = *name;
    const auto* padded = reinterpret_cast<const std::uint64_t*>(name);
    CHECK(padded[1] == 0);
    CHECK(padded[2] == 0);
    CHECK(padded[3] == 0);
    *category = calls.category;
    return calls.resolveResult;
}

std::uint64_t add(void* pouch, const char** name, void* modifier, std::uint32_t category,
                  std::int32_t count, std::uint8_t a, std::int32_t b, std::uint8_t c,
                  std::uint32_t d, std::uint32_t e, std::uint32_t f, std::uint32_t g,
                  std::uint32_t h, std::uint32_t i, std::uint32_t* j, std::uint8_t k) {
    ++calls.added;
    CHECK(*name == calls.actor);
    calls.arguments = {reinterpret_cast<std::uintptr_t>(pouch),
        reinterpret_cast<std::uintptr_t>(modifier), category, static_cast<std::uint32_t>(count),
        a, static_cast<std::uint32_t>(b), c, d, e, f, g, h, i, k};
    CHECK(j == nullptr);
    return calls.addResult;
}

struct Fixture {
    alignas(8) std::array<unsigned char, 512> manager{};
    alignas(8) std::array<unsigned char, 16> pouch{};
    void* modifier = pouch.data();

    Fixture() {
        calls = {};
        queue(200);
        queue(296);
    }

    std::uintptr_t address() { return reinterpret_cast<std::uintptr_t>(manager.data()); }
    void queue(std::size_t offset, int capacity = 32, std::uint32_t writeIndex = 0) {
        writeMemory(address() + offset + layout::kGameDataQueueCapacity, capacity);
        writeMemory(address() + offset + layout::kGameDataQueueBuffer, address());
        writeMemory(address() + offset + layout::kGameDataQueueControl, writeIndex);
    }
    KeyItemGrantResult grant(GameDataWriteReadiness readiness =
                            GameDataWriteReadiness::LiveGameplayAndWorkerAlive) {
        return grantKeyItem(address(), reinterpret_cast<std::uintptr_t>(pouch.data()),
                            "Obj_Champion", &modifier, {resolve, add}, readiness);
    }
};
}

TEST_CASE("Key Item grant preserves the released emblem's native call contract") {
    Fixture fixture;
    const auto result = fixture.grant();
    CHECK(result);
    CHECK(result.nativeResult == 37);
    CHECK(calls.resolved == 1);
    CHECK(calls.added == 1);
    CHECK(std::strcmp(calls.actor, "Obj_Champion") == 0);
    const std::array<std::uint64_t, 14> expected{
        reinterpret_cast<std::uintptr_t>(fixture.pouch.data()),
        reinterpret_cast<std::uintptr_t>(fixture.modifier),
        8, 1, 1, 0, 0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFFFFFu, 1};
    CHECK(calls.arguments == expected);
}

TEST_CASE("Key Item grant refuses stopped workers and either full integer queue") {
    Fixture fixture;
    CHECK(fixture.grant(GameDataWriteReadiness::NotReady).error == KeyItemGrantError::GameplayNotReady);
    for (const std::size_t offset : {200u, 296u}) {
        fixture.queue(offset, 32, 32);
        CHECK(fixture.grant().error == KeyItemGrantError::QueueUnavailable);
        fixture.queue(offset, 0);
        CHECK(fixture.grant().error == KeyItemGrantError::QueueUnavailable);
        fixture.queue(offset);
    }
    CHECK(calls.resolved == 0);
    CHECK(calls.added == 0);
    const auto invalid = grantKeyItem(0, 0, nullptr, nullptr, {},
        GameDataWriteReadiness::LiveGameplayAndWorkerAlive);
    CHECK(invalid.error == KeyItemGrantError::ManagerUnavailable);
}

TEST_CASE("Key Item grant reserves six writes plus the established queue headroom") {
    Fixture fixture;
    const auto boundary = static_cast<std::uint32_t>(32 - 6 - layout::kGameDataQueueHeadroom);
    fixture.queue(200, 32, boundary + 1);
    CHECK(fixture.grant().error == KeyItemGrantError::QueueUnavailable);
    fixture.queue(200, 32, boundary);
    CHECK(fixture.grant());
    CHECK(calls.added == 1);
}

TEST_CASE("Key Item grant preserves category and native refusals without claiming success") {
    Fixture fixture;
    calls.category = 5;
    CHECK(fixture.grant().error == KeyItemGrantError::WrongCategory);
    calls.category = 8;
    calls.resolveResult = 2;
    CHECK(fixture.grant().error == KeyItemGrantError::WrongCategory);
    CHECK(calls.added == 0);
    calls.resolveResult = 3;
    calls.addResult = 0;
    CHECK(fixture.grant().error == KeyItemGrantError::NativeRefusal);
    CHECK(calls.added == 1);
}
