// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "Healing.hpp"

#include <array>

namespace {
using namespace botw_abilities::champions;
using totk::engine::writeMemory;

struct Trace {
    LifeMessagePayload payload{};
    std::uint32_t id = 0;
    std::uintptr_t life = 0;
    std::uintptr_t context = 0;
    int received = 0;
    int processed = 0;
    std::uint8_t includePending = 255;
    bool accept = true;
} trace;

bool receive(std::uintptr_t life, const LifeMessage* message) {
    trace.life = life;
    trace.payload = *message->payload;
    trace.id = message->id;
    ++trace.received;
    CHECK(message->reserved == 0);
    CHECK(message->sender == 0);
    return trace.accept;
}

std::uintptr_t process(std::uintptr_t life, std::uintptr_t context, std::uint8_t pending) {
    CHECK(trace.received == 1);
    CHECK(life == trace.life);
    trace.context = context;
    trace.includePending = pending;
    ++trace.processed;
    return life;
}

template <std::size_t Size>
struct alignas(8) Memory {
    std::array<unsigned char, Size> bytes{};
    std::uintptr_t address() { return reinterpret_cast<std::uintptr_t>(bytes.data()); }
};

struct Fixture {
    Memory<64> extension;
    Memory<560> actor;
    Memory<768> registry;
    Memory<6224> life;
    Memory<16> current;
    Memory<16> maximum;
    Memory<16> extra;
    Memory<64> context;

    Fixture() {
        trace = {};
        writeMemory(extension.address() + 8, actor.address());
        writeMemory(actor.address() + 552, registry.address());
        writeMemory(registry.address() + 760, life.address());
        writeMemory(life.address() + 6192, current.address());
        writeMemory(life.address() + 6200, maximum.address());
        writeMemory(life.address() + 6208, extra.address());
        writeMemory(maximum.address() + 8, std::int32_t{120});
    }

    HealingResult prepare() {
        return prepareMiphaHealing(extension.address(), context.address(), {receive, process});
    }
};
}

TEST_CASE("Mipha requests five yellow hearts through the native message before returning full healing") {
    Fixture fixture;
    const auto result = fixture.prepare();
    REQUIRE(result);
    CHECK(result.health == 120);
    CHECK(trace.life == fixture.life.address());
    CHECK(trace.context == fixture.context.address());
    CHECK(trace.id == 0x46030000);
    CHECK(trace.payload.health == 0);
    CHECK(trace.payload.maxHealth == 0);
    CHECK(trace.payload.extraHealth == 20);
    CHECK(trace.payload.brokenHealth == 0);
    CHECK(trace.payload.flags == 0x800);
    CHECK(trace.processed == 1);
    CHECK(trace.includePending == 0);
    CHECK(fixture.extension.bytes[0x2C] == 0);
}

TEST_CASE("Mipha refuses unavailable player life data before calling native code") {
    Fixture fixture;
    writeMemory(fixture.registry.address() + 760, std::uintptr_t{0});
    CHECK(fixture.prepare().error == HealingError::MissingLife);
    writeMemory(fixture.registry.address() + 760, fixture.life.address());
    writeMemory(fixture.life.address() + 6208, std::uintptr_t{0});
    CHECK(fixture.prepare().error == HealingError::MissingValues);
    writeMemory(fixture.life.address() + 6208, fixture.extra.address());
    writeMemory(fixture.maximum.address() + 8, std::int32_t{0});
    CHECK(fixture.prepare().error == HealingError::InvalidMaxHealth);
    CHECK(trace.received == 0);
    CHECK(trace.processed == 0);
}

TEST_CASE("Mipha leaves native fairy fallback possible when the healing message is refused") {
    Fixture fixture;
    trace.accept = false;
    const auto result = fixture.prepare();
    CHECK_FALSE(result);
    CHECK(result.error == HealingError::MessageRefused);
    CHECK(result.health == 0);
    CHECK(trace.received == 1);
    CHECK(trace.processed == 0);
    CHECK(prepareMiphaHealing(0, 0, {}).error == HealingError::MissingFunction);
}
