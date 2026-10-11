// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombIdentity.hpp"
#include "BombMotion.hpp"
#include "BombMotionRequest.hpp"
#include <bit>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {
unsigned checks{};
alignas(16) std::array<std::uint8_t, 512> body{};
alignas(16) std::array<std::uint8_t, 224> request{};
unsigned locks{}, unlocks{}, allocations{};
bool locked{}, allocationAllowed = true;
void require(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
void lock(std::uintptr_t mutex) {
    require(mutex == reinterpret_cast<std::uintptr_t>(body.data()) + 120 && !locked, "correct native body mutex");
    locked = true;
    ++locks;
}
void unlock(std::uintptr_t mutex) {
    require(mutex == reinterpret_cast<std::uintptr_t>(body.data()) + 120 && locked, "scoped native unlock");
    locked = false;
    ++unlocks;
}
std::uintptr_t allocate(std::uintptr_t address) {
    require(address == reinterpret_cast<std::uintptr_t>(body.data()) && locked, "native queue allocation under body mutex");
    ++allocations;
    return allocationAllowed ? reinterpret_cast<std::uintptr_t>(request.data()) : 0;
}
template <class T> void write(auto &buffer, unsigned offset, T value) {
    std::memcpy(buffer.data() + offset, &value, sizeof(value));
}
template <class T> T read(const auto &buffer, unsigned offset) {
    T result{};
    std::memcpy(&result, buffer.data() + offset, sizeof(result));
    return result;
}
}
int main() {
    using namespace remote_bomb;
    using namespace remote_bomb::physics;
    MotionProperties original{};
    for (unsigned i = 0; i < original.prefix.size(); ++i)
        original.prefix[i] = 0x18203040U + i;
    for (unsigned i = 0; i < original.remainder.size(); ++i)
        original.remainder[i] = 0x51627380U + i;
    original.linearDamping = .003f;
    original.angularDamping = .087f;
    original.maxLinearVelocity = 1000.f;
    original.maxAngularVelocity = 1000.f;
    const auto retained = original;
    for (const auto form : {Shape::Sphere, Shape::Cube}) {
        const auto modified = withBombMotion(original, form);
        require(modified.maxLinearVelocity == 200.f, "original linear velocity limit");
        require(modified.maxAngularVelocity == 198.967529f, "original angular velocity limit in radians");
        require(modified.linearDamping == (form == Shape::Sphere ? .5f : 1.f), "original linear damping");
        require(modified.angularDamping == (form == Shape::Sphere ? .5f : 1.f), "original angular damping");
        const auto before = std::bit_cast<std::array<std::uint32_t, 28>>(original);
        const auto after = std::bit_cast<std::array<std::uint32_t, 28>>(modified);
        for (unsigned i = 0; i < before.size(); ++i)
            require((i >= 4 && i <= 7) || before[i] == after[i], "gravity, time scale and sleep bytes retained");
    }
    require(std::memcmp(&original, &retained, sizeof(original)) == 0, "shared source preset is immutable");
    require(motionPreset(40, 0, 82) == 40, "normal last preset");
    require(motionPreset(40, 0x10000000000ULL, 82) == 81, "water last preset");
    require(motionPreset(41, 0x10000000000ULL, 82) == 0, "invalid water index uses native fallback");
    require(motionPreset(0xFFFFFFFF, 0, 82) == 0, "negative native request uses fallback");
    require(motionPreset(0xFFFFFFFF, 0x10000000000ULL, 82) == 40, "native water addition preserves unsigned wrap");
    for (unsigned form = 0; form < 2; ++form)
        for (unsigned upgrade = 0; upgrade < 2; ++upgrade) {
            const auto identity = identifyBomb(kActorNames[form][upgrade]);
            require(identity && identity->shape == (form ? Shape::Cube : Shape::Sphere) &&
                    identity->upgrade == (upgrade ? Upgrade::Plus : Upgrade::Base), "all four exact private names classified");
        }
    require(!identifyBomb("BarrelBomb"), "donor actor is untouched");
    require(!identifyBomb("Obj_RemoteBombSpherePlusOther"), "prefix name is not a bomb");
    require(!identifyBomb("obj_RemoteBombCube"), "name comparison is case sensitive");
    require(!identifyBomb(""), "empty native name is ignored");
    const RequestBindings native{lock, unlock, allocate};
    const auto address = reinterpret_cast<std::uintptr_t>(body.data());
    write(body, 380, 12U);
    write(request, 132, 99U);
    write(request, 212, 0x40000020U);
    auto result = enqueueMotionRefresh(address, native);
    require(result.status == RefreshStatus::Queued, "unchanged native preset can be refreshed");
    require(read<unsigned>(request, 132) == 12 && read<unsigned>(request, 212) == 0x40000028U,
            "refresh changes only motion index and its queued bit");
    write(request, 132, 27U);
    result = enqueueMotionRefresh(address, native);
    require(result.status == RefreshStatus::AlreadyQueued && read<unsigned>(request, 132) == 27,
            "pending native motion change keeps its desired preset");
    write(body, 104, std::uint64_t(0x100));
    result = enqueueMotionRefresh(address, native);
    require(result.status == RefreshStatus::UnsupportedBody && allocations == 2,
            "native dummy queue is never changed");
    write(body, 104, std::uint64_t(4));
    result = enqueueMotionRefresh(address, native);
    require(result.status == RefreshStatus::UnsupportedBody && allocations == 2,
            "character body is never changed");
    write(body, 104, std::uint64_t(0));
    allocationAllowed = false;
    result = enqueueMotionRefresh(address, native);
    require(result.status == RefreshStatus::RequestUnavailable, "queue exhaustion is explicit");
    require(locks == 5 && unlocks == 5 && !locked, "all success and refusal paths release native mutex");
    std::cout << checks << " motion checks passed\n";
}
