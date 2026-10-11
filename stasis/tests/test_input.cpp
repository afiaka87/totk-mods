#include "doctest.h"
#include "StasisInput.hpp"
#include "totk/engine/Npad.hpp"
#include <array>
#include <cstring>
using namespace stasis::input;
namespace {
constexpr Wheel camera{41, true, true, false, false, false};
constexpr Wheel exiting{41, false, false, true, true, false};
constexpr Wheel closed{41, false, false, true, true, true};
template <class T> T read(std::uintptr_t p) {
    T out;
    std::memcpy(&out, reinterpret_cast<void *>(p), sizeof(T));
    return out;
}
template <class T> void put(std::uintptr_t p, T value) {
    std::memcpy(reinterpret_cast<void *>(p), &value, sizeof(T));
}
} // namespace
TEST_CASE("Camera detection reads native highlighted ability rather than a stick quadrant") {
    std::array<unsigned char, 672> screen{};
    const auto p = reinterpret_cast<std::uintptr_t>(screen.data());
    // Independent retail layout: eight entries; Camera is upper-right slot 1.
    put<std::int32_t>(p + 424, 8);
    put<std::int32_t>(p + 572, 1);
    put<std::uint8_t>(p + 581, 1);
    put<std::uint32_t>(p + 628, 7);
    const auto selected = [&] {
        return cameraSelected(p, []<class T>(std::uintptr_t a) { return read<T>(a); });
    };
    CHECK(selected());
    put<std::uint32_t>(p + 628, 2);
    CHECK_FALSE(selected());
    put<std::uint32_t>(p + 628, 7);
    put<std::uint8_t>(p + 581, 0);
    CHECK_FALSE(selected());
    put<std::uint8_t>(p + 581, 1);
    put<std::uint8_t>(p + 580, 1);
    CHECK_FALSE(selected());
    put<std::uint8_t>(p + 580, 0);
    for (const auto cursor : {-1, 8, 100}) {
        put<std::int32_t>(p + 572, cursor);
        CHECK_FALSE(selected());
    }
    put<std::int32_t>(p + 572, 1);
    for (const auto count : {0, 1, 9}) {
        put<std::int32_t>(p + 424, count);
        CHECK_FALSE(selected());
    }
    put<std::int32_t>(p + 424, 8);
    CHECK(selected());
    const auto copy = unpack(pack(closed));
    CHECK(copy.epoch == 41);
    CHECK(copy.exited);
    CHECK(copy.cancelled);
    CHECK(copy.closed);
    CHECK_FALSE(copy.open);
    CHECK_FALSE(copy.camera);
}
TEST_CASE("Native wheel cancel precedes Stasis aim and suppresses held L and scope") {
    Activation activation;
    auto out = activation.step(camera, L, L, true, false, true);
    CHECK(out.mask == 0);
    CHECK(out.add == 0);
    CHECK_FALSE(out.beginAim);
    out = activation.step(camera, L | StickR, StickR, true, false, true);
    CHECK(out.event == Event::Requested);
    CHECK(activation.pendingEpoch == 41);
    CHECK((out.mask & StickR) != 0);
    CHECK((out.mask & L) == 0);
    CHECK(out.add == B);
    CHECK_FALSE(out.beginAim);
    out = activation.step(exiting, L | StickR, 0, true, false, true);
    CHECK((out.mask & L) != 0);
    CHECK(out.add == 0);
    CHECK_FALSE(out.beginAim);
    out = activation.step(closed, L | StickR, 0, true, false, true);
    CHECK_FALSE(out.beginAim);
    CHECK(activation.pendingEpoch == 41);
    out = activation.step(closed, 0, 0, false, true, true);
    CHECK(out.beginAim);
    CHECK(out.event == Event::Activated);
    CHECK(activation.pendingEpoch == 0);
    CHECK((out.mask & (L | StickR)) == (L | StickR));
    out = activation.step(closed, L | StickR, 0, true, true, false);
    CHECK_FALSE(out.beginAim);
    CHECK((out.mask & (L | StickR)) == (L | StickR));
    out = activation.step(closed, 0, 0, true, true, true);
    CHECK(out.mask == 0);
    out = activation.step({}, L, L, true, true, true);
    CHECK(out.mask == 0);
    CHECK(out.add == 0);
    CHECK_FALSE(out.beginAim);
}
TEST_CASE("Ordinary wheel and Camera use pass through unless the explicit chord is pressed") {
    for (const auto wheel : {Wheel{}, Wheel{41, true, false}, camera}) {
        Activation a;
        const auto out = a.step(wheel, L, L, true, false, true);
        CHECK(out.mask == 0);
        CHECK(out.add == 0);
        CHECK(a.pendingEpoch == 0);
    }
    Activation a;
    auto out = a.step({41, true, false}, L | StickR, StickR, true, false, true);
    CHECK(out.mask == 0);
    CHECK(a.pendingEpoch == 0);
    out = a.step(camera, L | StickR, 0, true, false,
                 true); // Moving onto Camera with click already held.
    CHECK(out.mask == 0);
    CHECK(a.pendingEpoch == 0);
    out = a.step(camera, StickR, StickR, true, false, true);
    CHECK(out.mask == 0);
    CHECK(a.pendingEpoch == 0);
    out = a.step(camera, L | B | StickR, B | StickR, true, false, true);
    CHECK(out.mask == 0);
    CHECK(a.pendingEpoch == 0);
    out = a.step(camera, L | StickR, StickR, true, false, false); // Cooldown/frozen/aiming refusal.
    CHECK(out.event == Event::Unavailable);
    CHECK(out.add == 0);
    CHECK(a.pendingEpoch == 0);
    CHECK((out.mask & L) == 0);
    CHECK((out.mask & StickR) != 0);
    out = a.step(camera, L | StickR, 0, true, false, true);
    CHECK(a.pendingEpoch == 0);
    CHECK_FALSE(out.beginAim);
}
TEST_CASE(
    "Cancelled activation cannot leak across wheel epochs scene resets or failed native exits") {
    for (unsigned mode = 0; mode < 5; ++mode) {
        Activation a;
        a.step(camera, L | StickR, StickR, true, false, true);
        Decision out;
        switch (mode) {
        case 0:
            out = a.step({42, true, true}, L | StickR, 0, true, false, true);
            CHECK(out.event == Event::StaleWheel);
            break;
        case 1:
            out = a.step({41, false, false, true, false, true}, L, 0, true, true, true);
            CHECK(out.event == Event::NotCancelled);
            break;
        case 2:
            out = a.step(camera, L | B, B, true, false, true);
            CHECK(out.event == Event::UserCancelled);
            break;
        case 3:
            out = a.step({41, false, false, false, false, true}, L, 0, true, true, true);
            CHECK(out.event == Event::NotCancelled);
            break;
        default:
            for (unsigned n = 0; n < 241; ++n)
                out = a.step(camera, L, 0, true, false, true);
            CHECK(out.event == Event::TimedOut);
            break;
        }
        CHECK(a.pendingEpoch == 0);
        CHECK_FALSE(out.beginAim);
        CHECK(out.add == 0);
        out = a.step(closed, L | StickR, 0, true, true, true);
        CHECK_FALSE(out.beginAim);
        a.reset();
        out = a.step({}, L | StickR, 0, true, true, true);
        CHECK(a.pendingEpoch == 0);
        CHECK(out.mask == 0);
        CHECK(out.add == 0);
    }
    Activation a;
    a.step(camera, L | StickR, StickR, true, false, true);
    const auto out = a.step(closed, L, 0, true, true, false);
    CHECK(out.event == Event::Unavailable);
    CHECK_FALSE(out.beginAim);
    CHECK(a.pendingEpoch == 0);
}
TEST_CASE(
    "Wheel activation modifies only fresh native Npad samples and leaves other input intact") {
    using namespace totk::engine;
    std::array<unsigned char, layout::kNpadSlotStride * layout::kNpadSlotCount + 512> device{};
    const auto p = reinterpret_cast<std::uintptr_t>(device.data());
    const auto state = p + layout::kNpadState;
    constexpr std::uint64_t Y = 1ull << 3, ZL = 1ull << 8;
    put<std::int64_t>(state + layout::kNpadSamplingNumber, 1);
    put<std::uint64_t>(state + layout::kNpadButtons, L | StickR | Y | ZL);
    put<std::int32_t>(state + layout::kNpadRightStickX, 24000);
    put<std::int32_t>(state + layout::kNpadRightStickY, 24000);
    const auto stale = state + layout::kNpadSlotStride;
    put<std::uint64_t>(stale + layout::kNpadButtons, L | StickR | A);
    NpadReader reader;
    const auto frame = reader.read(device.data());
    REQUIRE(frame.snapshot().freshSampleCount == 1);
    Activation activation;
    const auto out = activation.step(camera, frame.snapshot().buttons, StickR, true, false, true);
    frame.maskOwnedButtons(out.mask);
    frame.addOwnedButtons(out.add);
    CHECK(read<std::uint64_t>(state + layout::kNpadButtons) == (L | B | Y | ZL));
    CHECK(read<std::uint64_t>(stale + layout::kNpadButtons) == (L | StickR | A));
    CHECK(read<std::int32_t>(state + layout::kNpadRightStickX) == 24000);
    CHECK(read<std::int32_t>(state + layout::kNpadRightStickY) == 24000);
    const auto repeated = reader.read(device.data());
    CHECK(repeated.snapshot().freshSampleCount == 0);
    repeated.maskOwnedButtons(~0ull);
    repeated.addOwnedButtons(1ull << 7);
    CHECK(read<std::uint64_t>(state + layout::kNpadButtons) == (L | B | Y | ZL));
}
TEST_CASE("Frozen B and L retain sprint and native ability controls without releasing Stasis") {
    Activation activation;
    for (const auto wheel :
         {closed, Wheel{42, true, false}, Wheel{42, false, false, true, false, false},
          Wheel{42, false, false, true, false, true}}) {
        for (const bool running : {false, true}) {
            const auto out = activation.step(wheel, L | B, L | B, true, running, false, true, 77);
            CHECK(out.mask == 0);
            CHECK(out.add == 0);
            CHECK_FALSE(out.releaseFreeze);
            CHECK_FALSE(out.beginAim);
        }
    }
    using namespace totk::engine;
    std::array<unsigned char, layout::kNpadSlotStride * layout::kNpadSlotCount + 512> device{};
    const auto state = reinterpret_cast<std::uintptr_t>(device.data()) + layout::kNpadState;
    put<std::int64_t>(state + layout::kNpadSamplingNumber, 1);
    put<std::uint64_t>(state + layout::kNpadButtons, L | A | B);
    NpadReader reader;
    const auto frame = reader.read(device.data());
    const auto out =
        activation.step(closed, frame.snapshot().buttons, L | B, true, true, false, true, 77);
    frame.maskOwnedButtons(out.mask);
    frame.addOwnedButtons(out.add);
    CHECK(read<std::uint64_t>(state + layout::kNpadButtons) == (L | A | B));
    CHECK_FALSE(out.releaseFreeze);
}
TEST_CASE(
    "Repeating the Camera chord releases the same freeze after native cancellation and resume") {
    Activation activation;
    auto out = activation.step(camera, L | StickR, StickR, true, false, false, true, 77);
    REQUIRE(out.event == Event::Requested);
    CHECK(activation.pendingRelease);
    CHECK(activation.pendingGeneration == 77);
    CHECK(out.add == B);
    CHECK((out.mask & L) == 0);
    CHECK_FALSE(out.releaseFreeze);
    CHECK_FALSE(out.beginAim);
    out = activation.step(exiting, L | StickR, 0, true, false, false, true, 77);
    CHECK_FALSE(out.releaseFreeze);
    CHECK(out.add == 0);
    CHECK((out.mask & L) != 0);
    out = activation.step(closed, L | StickR, 0, true, false, false, true, 77);
    CHECK_FALSE(out.releaseFreeze);
    out = activation.step(closed, 0, 0, false, true, false, true, 77);
    CHECK(out.releaseFreeze);
    CHECK(out.event == Event::Released);
    CHECK_FALSE(out.beginAim);
    CHECK(activation.pendingEpoch == 0);
    CHECK_FALSE(activation.pendingRelease);
    CHECK((out.mask & (L | StickR)) == (L | StickR));
    out = activation.step(closed, L | StickR, 0, true, true, false, false, 77);
    CHECK_FALSE(out.releaseFreeze);
    CHECK_FALSE(out.beginAim);
    out = activation.step(closed, 0, 0, true, true, false, false, 77);
    CHECK(out.mask == 0);
    CHECK(out.add == 0);
}
TEST_CASE("A pending release never becomes aiming or releases a different freeze") {
    for (unsigned mode = 0; mode < 5; ++mode) {
        Activation activation;
        activation.step(camera, L | StickR, StickR, true, false, false, true, 77);
        Decision out;
        switch (mode) {
        case 0:
            out = activation.step(closed, L, 0, true, true, true, false, 77);
            CHECK(out.event == Event::HoldLost);
            break;
        case 1:
            out = activation.step(closed, L, 0, true, true, false, true, 78);
            CHECK(out.event == Event::HoldLost);
            break;
        case 2:
            out = activation.step(camera, L | B, B, true, false, false, true, 77);
            CHECK(out.event == Event::UserCancelled);
            CHECK(out.mask == 0);
            break;
        case 3:
            out = activation.step({42, true, true}, L, 0, true, false, false, true, 77);
            CHECK(out.event == Event::StaleWheel);
            break;
        default:
            activation.reset();
            out = activation.step(closed, L, 0, true, true, true, false, 78);
            break;
        }
        CHECK_FALSE(out.releaseFreeze);
        CHECK_FALSE(out.beginAim);
        CHECK(out.add == 0);
        CHECK(activation.pendingEpoch == 0);
        out = activation.step(closed, L, 0, true, true, true, false, 78);
        CHECK_FALSE(out.releaseFreeze);
        CHECK_FALSE(out.beginAim);
    }
}
