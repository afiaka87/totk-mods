// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BlastDamage.hpp"
#include "NativeBlastDamage.hpp"
#include "NativeBlastContact.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
unsigned checks{};
void check(bool condition, const char *description) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "%s\n", description);
        std::exit(1);
    }
}
}
int main() {
    using namespace remote_bomb::blast;
    DamageSensor sensor;
    check(sensor.radius == .001f && sensor.remaining == 5.f && sensor.active, "original enter state");
    for (unsigned frame = 0; frame < 5; ++frame) {
        const std::array<float, 5> expected{2.001f, 4.001f, 6.f, 6.f, 6.f};
        check(sensor.advance(1.f) == DamageStep::Advanced, "five original calculations grow an active sensor");
        check(sensor.radius == expected[frame] && sensor.remaining == 4.f - float(frame) && sensor.active,
              "unit-frame radius and remaining time match the original trace");
    }
    check(sensor.advance(0.f) == DamageStep::Paused && sensor.active, "pause cannot expire the last active interval");
    check(sensor.advance(1.f) == DamageStep::Expired && !sensor.active, "next active calculation disables damage");
    check(sensor.advance(1.f) == DamageStep::Expired, "expiry cannot restart damage");
    sensor = {};
    check(sensor.advance(.5f) == DamageStep::Advanced && sensor.radius == 1.001f && sensor.remaining == 4.5f,
          "simulation half frame scales growth and duration");
    check(sensor.advance(0.f) == DamageStep::Paused && sensor.radius == 1.001f, "paused radius stays fixed");
    check(sensor.advance(-1.f) == DamageStep::Invalid && sensor.remaining == 4.5f, "negative time cannot rewind state");
    check(sensor.advance(std::numeric_limits<float>::infinity()) == DamageStep::Invalid, "infinite time is refused");
    check(sensor.advance(std::numeric_limits<float>::quiet_NaN()) == DamageStep::Invalid, "NaN time is refused");
    sensor = {};
    check(sensor.advance(6.f) == DamageStep::Advanced && sensor.radius == 6.f && sensor.active,
          "large step retains original test-before-decrement order");
    check(sensor.advance(.25f) == DamageStep::Expired, "overshoot expires on the next calculation");
    sensor = {};
    sensor.remaining = kDamageTimerEpsilon;
    check(sensor.advance(.25f) == DamageStep::Expired, "original epsilon boundary expires");

    DamageSources<2> sources;
    check(!sources.find(100, 200), "a damage contact cannot allocate a source window");
    check(sources.synchronize(1, 100, 200, {1, 2, 3}, 1.f, 1.f) == DamageSync::Started, "preparation starts damage");
    check(sources.find(100, 200)->sensor.radius == 2.001f, "initial native age includes its first simulation update");
    check(sources.synchronize(1, 100, 200, {4, 5, 6}, 1.f, 1.f) == DamageSync::Unchanged, "duplicate preparation cannot grow twice");
    check(sources.find(100, 200)->sensor.remaining == 4.f && sources.find(100, 200)->origin.x == 4.f,
          "same-age preparation updates location without consuming time");
    check(sources.synchronize(1, 100, 200, {4, 5, 6}, 2.f, 1.f) == DamageSync::Advanced, "next native age grows");
    check(sources.find(100, 200)->sensor.radius == 4.001f, "growth keeps original float accumulation");
    check(!sources.find(100, 201) && !sources.find(101, 200), "source object and actor must both match");
    check(sources.synchronize(1, 100, 201, {}, 3.f, 1.f) == DamageSync::IdentityChanged, "address reuse needs explicit removal");
    check(sources.synchronize(2, 101, 201, {}, 0.f, 1.f) == DamageSync::Started, "other bomb has independent state");
    check(sources.find(101, 201)->sensor.radius == .001f, "zero-age source starts at original tiny radius");
    check(sources.synchronize(3, 102, 202, {}, 1.f, 1.f) == DamageSync::Capacity, "bounded source overflow is explicit");
    check(sources.synchronize(1, 100, 200, {}, 1.f, 1.f) == DamageSync::Rewound, "rewound native identity is refused");
    check(!sources.find(100, 200)->sensor.active, "rewind closes stale damage");
    check(sources.remove(1)->nativeAge == 2.f && !sources.find(100, 200), "removal returns diagnostic state and clears identity");
    check(!sources.remove(1), "repeated removal is harmless");
    check(sources.synchronize(1, 102, 202, {}, 1.f, 1.f) == DamageSync::Started, "explicit removal allows address reuse");
    check(sources.synchronize(1, 102, 202, {}, 5.f, 4.f) == DamageSync::Advanced && sources.find(102, 202)->sensor.active,
          "damage window survives independent one-frame pressure expiry");
    check(sources.synchronize(1, 102, 202, {}, 6.f, 1.f) == DamageSync::Expired && !sources.find(102, 202)->sensor.active,
          "sixth preparation closes the five-frame damage interval");
    check(sources.synchronize(0, 102, 202, {}, 1.f, 1.f) == DamageSync::Invalid, "missing element cannot activate");
    check(sources.synchronize(3, 0, 202, {}, 1.f, 1.f) == DamageSync::Invalid, "missing object cannot activate");
    check(sources.synchronize(3, 102, 0, {}, 1.f, 1.f) == DamageSync::Invalid, "missing owner cannot activate");
    check(sources.synchronize(3, 102, 202, {}, -1.f, 1.f) == DamageSync::Invalid, "negative native age cannot activate");
    check(sources.synchronize(3, 102, 202, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, 1.f, 1.f) == DamageSync::Invalid,
          "invalid origin cannot activate");
    sources = {};
    check(!sources.find(101, 201), "scene reset clears both forms");
    DamageSensor fractional;
    float nativeAge{};
    for (unsigned step = 0; step < 10; ++step) {
        nativeAge += .1f;
        fractional.advance(.1f);
        sources.synchronize(1, 100, 200, {}, nativeAge, .1f);
        check(sources.find(100, 200)->sensor.radius == fractional.radius &&
              sources.find(100, 200)->sensor.remaining == fractional.remaining,
              "native delta avoids rounding differences from subtracting accumulated ages");
    }
    check(sources.synchronize(1, 100, 200, {}, nativeAge, -1.f) == DamageSync::Invalid,
          "invalid simulation delta cannot update state");

    std::array<unsigned char, 160> nativeInfo;
    nativeInfo.fill(0xA5);
    const auto before = nativeInfo;
    constexpr std::uintptr_t infoAddress = 0x60000000;
    for (const auto [offset, value] : std::array<std::pair<unsigned, std::uintptr_t>, 3>{{{0, 200}, {24, 100}, {32, 300}}})
        std::memcpy(nativeInfo.data() + offset, &value, sizeof(value));
    const auto unchanged = nativeInfo;
    auto read = [&]<class T>(std::uintptr_t address) {
        T value{};
        if (address >= infoAddress && address + sizeof(T) <= infoAddress + nativeInfo.size())
            std::memcpy(&value, nativeInfo.data() + (address - infoAddress), sizeof(value));
        return value;
    };
    const auto contact = remote_bomb::engine::explosion::chemicalDamageContact(infoAddress, read,
        [](std::uintptr_t address) { return address == infoAddress; });
    check(contact.actor == 200 && contact.source == 100 && contact.target == 300, "native constructor source+24 and target+32 decode");
    check(nativeInfo == unchanged && nativeInfo[16] == before[16], "decoding leaves attack parameter and native canaries intact");
    const auto missing = remote_bomb::engine::explosion::chemicalDamageContact(0, read,
        [](std::uintptr_t address) { return address == infoAddress; });
    check(!missing.actor && !missing.source && !missing.target, "invalid contact pointer is not dereferenced");
    std::array<unsigned char, 32> shock;
    shock.fill(0xA5);
    const auto beforeShock = shock;
    remote_bomb::engine::explosion::setShockDamageFlag(shock.data(), true);
    check(shock[24] == 1 && shock[25] == beforeShock[25], "damage flag is independent of native electric flag");
    remote_bomb::engine::explosion::setShockDamageFlag(shock.data(), false);
    check(shock[24] == 0, "expired damage clears notification input");
    for (unsigned index = 0; index < shock.size(); ++index)
        if (index != 24)
            check(shock[index] == beforeShock[index], "hit gating preserves pressure, point and result canaries");
    std::printf("%u blast damage checks passed\n", checks);
}
