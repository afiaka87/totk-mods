// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <vector>
#include <array>
#include "NativeBombActions.hpp"
#include "NativePlayerKeys.hpp"
#include "NativeAbilityAdmission.hpp"

namespace {
using namespace remote_bomb::engine;
unsigned checks{}, detonations{}, getters{}, releases{}, deletions{}, deaths{};
bool deathAccepted = true;
bool chemicalPresent = true, lifterPresent = true;
int playerObject{}, bombObject{}, chemicalObject{};
std::array<unsigned char, 512> lifterObject{};
void require(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
void abilityKeys() {
    std::array<unsigned char, 256> memory{};
    unsigned reads{}, writes{};
    const auto read = [&](std::uintptr_t address) {
        ++reads;
        std::uint64_t value;
        std::memcpy(&value, memory.data() + address, sizeof(value));
        return value;
    };
    const auto write = [&](std::uintptr_t address, std::uint64_t value) {
        ++writes;
        std::memcpy(memory.data() + address, &value, sizeof(value));
    };
    const auto valid = [](std::uintptr_t address) { return address >= 128 && address <= 248; };
    write(40, 128); write(56, 144); write(72, 160);
    write(128, 0x20000 | 0x10090); write(144, 0x20000 | 0x10010); write(160, 0x240);
    const auto untouched = memory;
    reads = writes = 0;
    auto result = filterAbilityKeys(0, false, read, write, valid);
    require(!result.changed && !result.invalid && !reads && !writes && memory == untouched,
            "native selection bypasses the adapter without any memory access");
    result = filterAbilityKeys(0, true, read, write, valid);
    require(result.changed == 3 && !result.invalid && writes == 2,
            "held and pressed ability outputs are each written once through their direct pointer");
    require(read(128) == 0x10090 && read(144) == 0x10010,
            "native placement and throw input bits survive ability suppression");
    require(read(40) == 128 && read(56) == 144 && read(72) == 160 && read(160) == 0x240,
            "node pointers and the native release output are preserved");
    write(128, 0); write(144, 0x20000);
    result = filterAbilityKeys(0, true, read, write, valid);
    require(result.changed == 2 && !result.invalid && read(128) == 0 && read(144) == 0,
            "idle held value is data, and a pressed-only L still gets consumed");
    write(40, 0); write(144, 0x20000 | 4);
    result = filterAbilityKeys(0, true, read, write, valid);
    require(result.invalid == 1 && result.changed == 2 && read(144) == 4,
            "one invalid output is reported without losing suppression of the other");
}
void abilityAdmission() {
    using namespace remote_bomb::input;
    unsigned originalCalls{}, blockedCalls{};
    std::uintptr_t lastActor{};
    const auto original = [&](std::uintptr_t actor) -> std::uint64_t {
        ++originalCalls;
        lastActor = actor;
        return 0x123456789abcdef1ull;
    };
    const auto blocked = [&](std::uintptr_t actor) { ++blockedCalls; lastActor = actor; };
    State controls;
    controls.remembered = true;
    auto selected = transition(controls, {{7}, 0, 0, true, true, true});
    require(selected.event == Event::None && selected.next.remembered,
            "a bomb taken from Materials owns recent ability admission");
    require(recentAbilityAdmission(128, selected.next.remembered, 128, original, blocked) == 0 &&
                !originalCalls && blockedCalls == 1 && lastActor == 128,
            "owned player cannot enter native ability or execute its side effects");
    auto paused = transition(selected.next, {{8, true}, L, L, true, false, true});
    require(recentAbilityAdmission(128, paused.next.remembered, 128, original, blocked) == 0 &&
                !originalCalls,
            "wheel pause retains admission ownership");
    auto cancelled = transition(paused.next, {{8, false, true, true, true}, 0, 0,
                                             true, true, true});
    require(recentAbilityAdmission(128, cancelled.next.remembered, 128, original, blocked) == 0 &&
                !originalCalls && blockedCalls == 3,
            "cancelled native wheel cannot briefly start the remembered native ability");
    require(recentAbilityAdmission(256, true, 128, original, blocked) == 0x123456789abcdef1ull &&
                originalCalls == 1 && lastActor == 256 && blockedCalls == 3,
            "another actor preserves the original return value and side effects");
    auto native = transition(cancelled.next, {{9, false, true, false, true}, 0, 0,
                                             true, true, true, true});
    require(recentAbilityAdmission(128, native.next.remembered, 128, original, blocked) ==
                0x123456789abcdef1ull && originalCalls == 2 && blockedCalls == 3,
            "accepted native selection immediately restores its original admission");
    require(recentAbilityAdmission(128, true, 0, original, blocked) == 0x123456789abcdef1ull &&
                originalCalls == 3 && blockedCalls == 3,
            "scene invalidation removes the player identity and all interception");
    require(recentAbilityAdmission(0, true, 0, original, blocked) == 0x123456789abcdef1ull &&
                originalCalls == 4 && blockedCalls == 3,
            "null scene identities cannot falsely claim a native action");
}
void *getLifter(void *actor) {
    ++getters;
    require(actor == &playerObject, "lifter getter receives player");
    return lifterPresent ? &lifterObject : nullptr;
}
void *chemical(void *actor) {
    ++getters;
    require(actor == &bombObject, "chemical getter receives selected bomb");
    return chemicalPresent ? &chemicalObject : nullptr;
}
void detonate(void *component, unsigned reason, int index, const Position *position) {
    ++detonations;
    require(component == &chemicalObject && reason == 1 && index == -1,
            "detonation matches stock OneShotChemicalRequestDetonate");
    require(std::isnan(position->x), "native inherited-position sentinel");
}
bool exploded(void *component, int index) {
    require(component == &chemicalObject && index == -1, "native explosion observation");
    return true;
}
NativeReference heldReference(void *link) {
    require(link == lifterObject.data() + 200, "held reference uses verified embedded MemoryActorLink");
    return {&bombObject, 1};
}
void releaseReference(NativeReference *reference) {
    ++releases;
    require(reference->value == &bombObject && reference->counted, "native reference releases once while counted");
    reference->value = nullptr;
    reference->counted = 0;
}
bool deleteActor(void *actor, unsigned reason) {
    ++deletions;
    require(actor == &bombObject && reason == 0, "discard deletes only its own bomb");
    return true;
}
bool requestDeath(void *actor, unsigned reason, unsigned subreason) {
    ++deaths;
    require(actor == &bombObject && reason == 8 && subreason == 0,
            "death-drop route matches native chemical explosion death");
    return deathAccepted;
}
}
int main() {
    abilityKeys();
    abilityAdmission();
    Bindings native{getLifter, chemical, detonate, exploded, heldReference, releaseReference, deleteActor, requestDeath};
    const Actor player{&playerObject, 7}, bomb{&bombObject, 7};
    const auto before = getters;
    require(requestDetonation(native, 8, bomb) == Status::StaleActor && !carriedActor(native, 8, player).value &&
                !discard(native, 8, bomb),
            "stale actors are refused");
    require(getters == before && !deletions, "stale actors are never dereferenced");
    require(requestDetonation(native, 7, bomb) == Status::Accepted && detonations == 1,
            "direct remote request reaches native chemical");
    require(explosionReported(native, 7, bomb), "explosion confirmation is separate from request");
    require(carriedActor(native, 7, player).value == &bombObject && releases == 1,
            "carried query releases the temporary native reference");
    lifterPresent = false;
    require(!carriedActor(native, 7, player).value && releases == 1, "missing lifter borrows nothing");
    lifterPresent = true;
    require(discard(native, 7, bomb) && deletions == 1, "spent bomb is deleted through the native request");
    chemicalPresent = false;
    require(requestDetonation(native, 7, bomb) == Status::MissingChemical,
            "missing chemical returns actionable status");
    require(detonations == 1, "missing component cannot trigger another request");
    require(requestDetonation(native, 8, bomb, DetonationRoute::DeathDrop) == Status::StaleActor && deaths == 0,
            "death-drop route never touches stale actors");
    const auto chemicalReads = getters;
    require(requestDetonation(native, 7, bomb, DetonationRoute::DeathDrop) == Status::Accepted && deaths == 1 &&
                detonations == 1 && getters == chemicalReads,
            "native sphere death-drop route works with chemical explosion disabled");
    deathAccepted = false;
    require(requestDetonation(native, 7, bomb, DetonationRoute::DeathDrop) == Status::DeathRefused && deaths == 2,
            "rejected death stays retryable without claiming an explosion");
    native.requestDeath = nullptr;
    require(requestDetonation(native, 7, bomb, DetonationRoute::DeathDrop) == Status::MissingBindings && deaths == 2,
            "missing death binding never falls back to chemical detonation");
    native.releaseReference = nullptr;
    require(!carriedActor(native, 7, player).value, "a missing reference destructor prevents borrowing");
    Bindings empty;
    require(requestDetonation(empty, 7, bomb) == Status::MissingBindings, "missing bindings refused");
    require(!bindTotk121(0, empty), "null module never binds native addresses");
    BindingFailure failure;
    require(!bindTotk121(0, empty, &failure) && failure.error == BindingError::NullImage,
            "null image gives an actionable binding diagnostic");
    std::vector<unsigned char> image(0x1600000);
    const auto base = reinterpret_cast<std::uintptr_t>(image.data());
    require(!bindTotk121(base, empty, &failure) && failure.error == BindingError::SignatureMismatch &&
                failure.offset == 0x11523B8 && failure.actual == 0,
            "wrong game binary reports the first rejected address and its bytes");
    require(!empty.getLifter && !empty.detonate, "failed binding leaves no callable native functions");
    struct Fixture { unsigned offset; std::uint64_t bytes; };
    const Fixture fixtures[]{
        {0x11523B8, 0xB4000068F9411408}, {0x102A3C0, 0xB4000068F9411408},
        {0x158B304, 0xA9017BFDD100C3FF}, {0x1593CA8, 0xA9BE7BFD37F803A1},
        {0x6DBC20, 0xA9014FF4A9BE7BFD}, {0xF85448, 0x3500008839402008},
        {0x83AA68, 0xA9017BFDD10143FF},
        {0x9F9B28, 0xB40000E8F9411408},
    };
    for (const auto fixture : fixtures)
        std::memcpy(image.data() + fixture.offset, &fixture.bytes, sizeof(fixture.bytes));
    require(bindTotk121(base, empty, &failure) && failure.error == BindingError::None,
            "exact relocated image binds successfully");
    std::cout << checks << " native adapter checks passed\n";
}
