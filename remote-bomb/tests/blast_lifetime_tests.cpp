// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BlastLifetime.hpp"
#include "BlastDamage.hpp"
#include "NativeBlastLifetime.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
unsigned checks{};
void check(bool value, const char *message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
}
int main() {
    using namespace remote_bomb::blast;
    Lifetimes<2> lifetime;
    DamageSources<2> damage;
    check(lifetime.arm(1, 100, 200) == LifetimeArm::Started, "instance creation retains its owner before preparation");
    check(lifetime.findActor(200)->phase == LifetimePhase::AwaitingPreparation,
          "early native cleanup cannot precede the first damage preparation");
    check(!damage.find(100, 200), "retention alone cannot admit damage or pressure");
    check(lifetime.arm(1, 100, 200) == LifetimeArm::Existing, "duplicate instance callback does not restart lifetime");
    check(lifetime.arm(1, 101, 200) == LifetimeArm::IdentityChanged, "owner object reuse is refused");
    check(lifetime.arm(2, 101, 201) == LifetimeArm::Started, "second form retains an independent owner");
    check(lifetime.arm(3, 102, 202) == LifetimeArm::Capacity, "source storage is bounded");
    for (unsigned frame = 1; frame <= 5; ++frame) {
        damage.synchronize(1, 100, 200, {}, float(frame), 1.f);
        lifetime.observe(1, damage.find(100, 200)->sensor.active);
        check(lifetime.findActor(200).has_value(), "native clear and death attempts stay deferred in all five damage intervals");
        check(lifetime.findActor(201)->phase == LifetimePhase::AwaitingPreparation,
              "first form calculations cannot advance the other form");
    }
    damage.synchronize(1, 100, 200, {}, 5.f, 0.f);
    lifetime.observe(1, damage.find(100, 200)->sensor.active);
    check(lifetime.findActor(200).has_value(), "pause retains the fifth interval even with zero time remaining");
    damage.synchronize(1, 100, 200, {}, 6.f, 1.f);
    lifetime.observe(1, damage.find(100, 200)->sensor.active);
    check(!lifetime.findActor(200), "expiry lets native clear and death resume");
    lifetime.observe(1, true);
    check(!lifetime.findActor(200), "a stale callback cannot reopen a finished lifetime");
    check(lifetime.remove(1)->phase == LifetimePhase::Finished, "instance removal returns and clears retained identity");
    check(!lifetime.remove(1), "duplicate removal is harmless");
    check(lifetime.arm(1, 102, 202) == LifetimeArm::Started, "explicit removal permits fresh source reuse");
    lifetime.observe(1, false);
    check(!lifetime.findActor(202), "invalid preparation releases retention instead of stranding cleanup");
    check(!lifetime.observe(0, true) && !lifetime.observe(9, true), "foreign callbacks cannot create retention");
    check(lifetime.arm(0, 100, 200) == LifetimeArm::Invalid && lifetime.arm(4, 0, 200) == LifetimeArm::Invalid &&
          lifetime.arm(4, 100, 0) == LifetimeArm::Invalid, "incomplete identities cannot retain native objects");
    lifetime = {};
    check(!lifetime.findActor(201), "scene reset releases every retained owner");

    std::array<unsigned char, 768> object;
    object.fill(0xA5);
    constexpr std::uintptr_t base = 0x60000000, lists = 0x61000000;
    unsigned count = 2;
    std::memcpy(object.data() + 704, &count, sizeof(count));
    std::memcpy(object.data() + 712, &lists, sizeof(lists));
    std::memcpy(object.data() + 24, &base, sizeof(base));
    const auto before = object;
    unsigned reads{};
    auto read = [&]<class T>(std::uintptr_t address) {
        ++reads;
        T value{};
        if (address >= base && address + sizeof(T) <= base + object.size())
            std::memcpy(&value, object.data() + (address - base), sizeof(value));
        return value;
    };
    const auto valid = [](std::uintptr_t address) { return address == base || address == lists; };
    using namespace remote_bomb::engine::explosion;
    check(explosionElementList(base, read, valid) == lists + 32, "native explosion list is the second 32-byte entry");
    check(deathProcedureActor(base, read, valid) == base, "native death component owns actor at byte24");
    check(object == before, "lifetime inspection preserves every native byte");
    for (unsigned state = 0; state <= 6; ++state) {
        std::memcpy(object.data() + 36, &state, sizeof(state));
        check(calculatingBombOwner(base, read, valid) == (state == 4),
              "only native calculation state4 retains owners; load/sleep/delete cleanup proceeds");
    }
    count = 1;
    std::memcpy(object.data() + 704, &count, sizeof(count));
    check(!explosionElementList(base, read, valid), "missing explosion list cannot alias the first chemical list");
    count = 2;
    std::memcpy(object.data() + 704, &count, sizeof(count));
    check(!explosionElementList(base, read, [](std::uintptr_t a) { return a == base; }), "invalid list pointer is refused");
    reads = 0;
    check(!explosionElementList(0, read, valid) && !deathProcedureActor(0, read, valid) &&
          !calculatingBombOwner(0, read, valid) && !reads,
          "invalid native object addresses are never dereferenced");
    std::printf("%u blast lifetime checks passed\n", checks);
}
