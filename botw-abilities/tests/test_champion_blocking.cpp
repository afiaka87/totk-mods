// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "Blocking.hpp"
#include "totk/engine/ActorReference121.hpp"
#include <thread>
#include <vector>

using namespace botw_abilities::champions;
namespace {
States initialStates() {
    States result;
    for (unsigned i = 0; i < result.size(); ++i) result[i] = initial(static_cast<Blessing>(i));
    return result;
}
unsigned referenceResolves{}, referenceClears{}, liveReferences{};
struct ReferenceInput { const void* actor = nullptr; std::uint8_t counted = 0; };
totk::engine::NativeActorReferenceResult resolveReference(const void* raw) {
    ++referenceResolves;
    const auto& input = *static_cast<const ReferenceInput*>(raw);
    if (input.actor && input.counted) ++liveReferences;
    return {input.actor,input.counted};
}
void clearReference(totk::engine::NativeActorReferenceResult* reference, const void* argument) {
    CHECK(argument == nullptr);
    ++referenceClears;
    if (reference->actor && reference->counted) --liveReferences;
    reference->actor = nullptr; reference->counted = 0;
}
}

TEST_CASE("Native actor references retain and release counted and borrowed actors exactly once") {
    using namespace totk::engine;
    referenceResolves = referenceClears = liveReferences = 0;
    const ActorReference121Functions functions{resolveReference,clearReference};
    int actor{};
    const ReferenceInput counted{&actor,1}, borrowed{&actor,0}, missing{};
    {
        ActorReference121 first(functions,&counted);
        CHECK(first.get() == &actor);
        CHECK(liveReferences == 1);
        { ActorReference121 second(functions,&borrowed); CHECK(second.get() == &actor); }
        CHECK(liveReferences == 1);
        CHECK(referenceClears == 1);
        { ActorReference121 absent(functions,&missing); CHECK(absent.get() == nullptr); }
        CHECK(referenceClears == 1);
    }
    CHECK(liveReferences == 0);
    CHECK(referenceResolves == 3);
    CHECK(referenceClears == 2);
    static_assert(!std::is_copy_constructible_v<ActorReference121> && !std::is_move_constructible_v<ActorReference121>);
}

TEST_CASE("Shield wear probe discards native flag changes only when actual damaging contacts are blocked") {
    std::array<std::byte,32> original{};
    original[24] = std::byte{0x20};
    const auto before = original;
    unsigned decisions = 0, clears = 0;
    bool active = true;
    const auto evaluate = [&](void* copy) {
        CHECK(copy != original.data());
        static_cast<std::byte*>(copy)[24] |= std::byte{0x88};
    };
    const auto protectedContacts = [&] { ++decisions; return true; };
    const auto clear = [&] { ++clears; active = false; };
    const auto protectedHit = probeEquipmentWear(original,evaluate,[&] { return active; },protectedContacts,clear);
    CHECK(protectedHit.blocked);
    CHECK(protectedHit.flagDelta == 0);
    CHECK(original == before);
    CHECK(decisions == 1);
    CHECK(clears == 1);
    active = true;
    const auto mixed = probeEquipmentWear(original,evaluate,[&] { return active; },[] { return false; },clear);
    CHECK_FALSE(mixed.blocked);
    CHECK(mixed.flagDelta == 0x88);
    CHECK(clears == 1);
    CHECK(original == before);
    active = false;
    const auto harmless = probeEquipmentWear(original,evaluate,[&] { return active; },protectedContacts,clear);
    CHECK_FALSE(harmless.blocked);
    CHECK(harmless.flagDelta == 0x88);
    CHECK(decisions == 1);
    CHECK(clears == 1);
}

TEST_CASE("Daruk deduplicates damage shield wear and recoil for one attack across concurrent callbacks") {
    SavedState state;
    REQUIRE(state.restore(0, initialStates()));
    BlockLedger ledger;
    std::atomic<unsigned> charged{}, blocked{};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 12; ++i) workers.emplace_back([&] {
        const auto result = ledger.block(state, 0, 42, {true}, true, {{17,2},{18,2}}, 100);
        if (result.status == BlockStatus::Blocked) ++charged;
        if (result) ++blocked;
    });
    for (auto& worker : workers) worker.join();
    CHECK(charged == 1);
    CHECK(blocked == 12);
    CHECK(state.snapshot().states[2].charges == 5);
    CHECK_FALSE(ledger.block(state, 0, 42, {true}, true, {{17,2},{19,2}}, 101));
    CHECK_FALSE(ledger.observeGuard(0,42,true));
    CHECK_FALSE(ledger.observeGuard(0,42,false));
    CHECK(ledger.observeGuard(0,42,true));
    CHECK(ledger.block(state, 0, 42, {true}, true, {{17,2},{19,2}}, 102).status == BlockStatus::Blocked);
    CHECK(state.snapshot().states[2].charges == 4);
}

TEST_CASE("Guard follow-up cancels only wholly protected primary attacks in the current save scene and tick") {
    GuardContacts contacts;
    const AttackId first{1,2}, second{2,2};
    CHECK_FALSE(contacts.protectedPrimary(0,42,100,first));
    contacts.record(0,42,100,first,true);
    contacts.record(0,42,100,second,false);
    CHECK(contacts.protectedPrimary(0,42,100,first));
    CHECK_FALSE(contacts.protectedPrimary(0,42,100,second));
    CHECK_FALSE(contacts.protectedPrimary(0,42,100,{1,3}));
    CHECK_FALSE(contacts.protectedPrimary(1,42,100,first));
    CHECK_FALSE(contacts.protectedPrimary(0,43,100,first));
    CHECK_FALSE(contacts.protectedPrimary(0,42,101,first));
    contacts.record(0,42,100,first,false);
    contacts.record(0,42,100,first,true);
    CHECK_FALSE(contacts.protectedPrimary(0,42,100,first));
    contacts.record(0,42,101,first,true);
    CHECK(contacts.protectedPrimary(0,42,101,first));
    CHECK_FALSE(contacts.protectedPrimary(0,42,100,first));
}

TEST_CASE("Guard follow-up preserves native behavior on missing IDs or a full contact table") {
    GuardContacts<2> contacts;
    contacts.record(0,42,100,{1,2},true);
    contacts.record(0,42,100,{2,2},true);
    CHECK(contacts.protectedPrimary(0,42,100,{1,2}));
    contacts.record(0,42,100,{3,2},true);
    CHECK_FALSE(contacts.protectedPrimary(0,42,100,{1,2}));
    CHECK_FALSE(contacts.protectedPrimary(0,42,100,{3,2}));
    contacts.record(0,42,101,{1,2},true);
    CHECK(contacts.protectedPrimary(0,42,101,{1,2}));
    contacts.record(0,42,101,{},true);
    CHECK_FALSE(contacts.protectedPrimary(0,42,101,{1,2}));
    contacts.record(1,42,101,{1,2},true);
    CHECK(contacts.protectedPrimary(1,42,101,{1,2}));
    CHECK_FALSE(contacts.protectedPrimary(0,42,101,{1,2}));
    contacts.record(1,43,101,{2,2},true);
    CHECK_FALSE(contacts.protectedPrimary(1,43,101,{1,2}));
    CHECK(contacts.protectedPrimary(1,43,101,{2,2}));
}

TEST_CASE("Concurrent guard contacts retain any unprotected secondary contact") {
    GuardContacts contacts;
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 12; ++i)
        workers.emplace_back([&,i] { contacts.record(0,42,100,{1,2},i != 5); });
    for (auto& worker : workers) worker.join();
    CHECK_FALSE(contacts.protectedPrimary(0,42,100,{1,2}));
}

TEST_CASE("Daruk protects all records of the sixth hit without granting a seventh charge") {
    SavedState state;
    REQUIRE(state.restore(0, initialStates()));
    BlockLedger ledger;
    for (unsigned i = 1; i <= 6; ++i) {
        ledger.observeGuard(0,42,false);
        ledger.observeGuard(0,42,true);
        const auto result = ledger.block(state, 0, 42, {true}, true, {{i,2},{}}, 100);
        REQUIRE(result.status == BlockStatus::Blocked);
        CHECK(result.claim.use.state.charges == 6 - i);
    }
    CHECK(state.snapshot().states[2].remainingSeconds == 1080);
    CHECK(ledger.block(state, 0, 42, {true}, true, {{6,2},{}}, 101).status == BlockStatus::Repeated);
    CHECK_FALSE(ledger.block(state, 0, 42, {true}, true, {{7,2},{}}, 101));
}

TEST_CASE("Daruk duplicate immunity expires without sliding its window on repeated contacts") {
    SavedState state;
    REQUIRE(state.restore(0, initialStates()));
    BlockLedger ledger;
    const AttackKey key{{99,2},{100,2}};
    REQUIRE(ledger.block(state, 0, 42, {true}, true, key, 0).status == BlockStatus::Blocked);
    CHECK(ledger.block(state, 0, 42, {true}, true, key, ledger.window - 1).status == BlockStatus::Repeated);
    CHECK_FALSE(ledger.block(state, 0, 42, {true}, true, key, ledger.window));
    ledger.observeGuard(0,42,false);
    CHECK_FALSE(ledger.block(state, 0, 42, {true}, true, key, ledger.window));
    ledger.observeGuard(0,42,true);
    CHECK(ledger.block(state, 0, 42, {true}, true, key, ledger.window).status == BlockStatus::Blocked);
    CHECK(state.snapshot().states[2].charges == 4);
}

TEST_CASE("Daruk refuses stale saves disabled items shrines and unguarded hits before reusing a block") {
    SavedState state;
    REQUIRE(state.restore(0, initialStates()));
    BlockLedger ledger;
    const AttackKey key{{99,2},{100,2}};
    REQUIRE(ledger.block(state, 0, 42, {true}, true, key, 1));
    CHECK_FALSE(ledger.block(state, 0, 42, {}, true, key, 2));
    CHECK_FALSE(ledger.block(state, 0, 42, {true,true}, true, key, 2));
    CHECK_FALSE(ledger.block(state, 0, 42, {true}, false, key, 2));
    CHECK(ledger.block(state, 0, 42, {true}, true, {}, 2).status == BlockStatus::MissingId);
    REQUIRE(state.enable(0, Blessing::Daruk, false));
    CHECK_FALSE(ledger.block(state, 0, 42, {true}, true, key, 2));
    const auto next = state.invalidate();
    REQUIRE(state.restore(next, initialStates()));
    CHECK(ledger.block(state, 0, 42, {true}, true, key, 2).status == BlockStatus::Stale);
    CHECK(ledger.block(state, next, 42, {true}, true, key, 2).status == BlockStatus::Blocked);
    CHECK(ledger.block(state, next, 43, {true}, true, key, 2).status == BlockStatus::Blocked);
}
