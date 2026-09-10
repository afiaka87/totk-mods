#include <array>
#include <atomic>
#include <limits>
#include <thread>

#include "RecallWristProvider.hpp"
#include "RecallPresentationFrame.hpp"
#include "RecallWristEmitter.hpp"
#include "RecallPosePresentation.hpp"
#include "doctest.h"

using namespace self_recall::pure;

TEST_CASE("presentation seals fractional translation with the animation key") {
    PresentationFrameLatch<PosePresentation> latch;
    PosePresentation first{{9, 1, 0}, {-1.75f, 4.5f, 0.25f}};
    REQUIRE(latch.publish(1, 10, first));
    PosePresentation queued = first;
    queued.offset.y = 30;
    CHECK(latch.publish(1, 10, queued).offset.y == 4.5f);
    CHECK(latch.snapshot(1).offset.x == -1.75f);
    CHECK(latch.snapshot(1).key == first.key);
    CHECK(latch.publish(1, 11, queued).offset.y == 30);
    CHECK_FALSE(latch.snapshot(2));
}

TEST_CASE("native effect-local transform survives late wrist translation rotation and scale") {
    const float wrist[12]{0,-2,0,100, 3,0,0,200, 0,0,4,-50};
    const float effect[12]{2,0,0,99.6f, 0,3,0,199.85f, 0,0,2,-48.4f};
    float local[12];
    REQUIRE(relativeEffectMatrix(wrist, effect, local));
    const float current[12]{1,0,0,200, 0,1,0,-100, 0,0,1,20};
    const float origin[3]{10,20,30};
    float columns[16];
    REQUIRE(emitterMatrix(current, local, origin, columns));
    const float expected[16]{0,-1,0,0, 1,0,0,0, 0,0,0.5f,0, 209.95f,-79.8f,50.4f,0};
    for (unsigned i = 0; i < 16; ++i) CHECK(columns[i] == doctest::Approx(expected[i]).epsilon(0.0001));
    float singular[12]{};
    CHECK_FALSE(relativeEffectMatrix(singular, effect, local));
    singular[0] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(relativeEffectMatrix(singular, effect, local));
}

TEST_CASE("emitter bindings reject recycled native sets and survive finite capacity teardown") {
    WristEmitterFrames<2> frames;
    WristEmitterFrame first{0x1000, 42, {1, 7}, {}};
    REQUIRE(frames.put(first));
    CHECK(frames.get(0x1000, 42).owner.serial == 1);
    CHECK_FALSE(frames.get(0x1000, 43).owner);
    first.instance = 43;
    first.owner = {2, 8};
    REQUIRE(frames.put(first));
    CHECK_FALSE(frames.get(0x1000, 42).owner);
    CHECK(frames.get(0x1000, 43).owner.serial == 2);
    first.emitterSet = 0x2000;
    REQUIRE(frames.put(first));
    first.emitterSet = 0x3000;
    CHECK_FALSE(frames.put(first));
    frames.clear();
    CHECK_FALSE(frames.get(0x1000, 43).owner);
    CHECK(frames.put(first));
}

TEST_CASE("only the native frame publisher can advance controller body and wrist presentation") {
    PresentationFrameLatch latch;
    const PoseFrameKey first{101, 4, 2}, advanced{100, 4, 1};
    CHECK_FALSE(latch.snapshot(1));
    CHECK(latch.publish(1, 20, first) == first);
    for (int reader = 0; reader < 3; ++reader) CHECK(latch.snapshot(1) == first);
    CHECK(latch.publish(1, 20, advanced) == first);
    CHECK(latch.publish(1, 21, advanced) == advanced);
    CHECK_FALSE(latch.publish(1, 20, first));
    CHECK(latch.snapshot(1) == advanced);
    CHECK(latch.publish(2, 21, first) == first);
    CHECK_FALSE(latch.publish(1, 22, advanced));
    CHECK_FALSE(latch.snapshot(1));
}

TEST_CASE("simultaneous presentation consumers cannot choose different frames") {
    PresentationFrameLatch latch;
    const PoseFrameKey expected{100, 4, 2};
    REQUIRE(latch.publish(1, 10, expected) == expected);
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    PoseFrameKey a, b;
    const auto consume = [&](PoseFrameKey& out) {
        ++ready;
        while (!start.load()) {}
        out = latch.snapshot(1);
    };
    std::thread first(consume, std::ref(a));
    std::thread second(consume, std::ref(b));
    while (ready.load() != 2) {}
    start.store(true);
    first.join(); second.join();
    REQUIRE(a);
    CHECK(a == b);
    CHECK(a == expected);
}

TEST_CASE("accelerated presentation cadence does not depend on late consumer order") {
    for (const unsigned quarters : {7u, 8u, 16u}) {
        PresentationFrameLatch latch;
        for (unsigned clock = 1; clock <= 60; ++clock) {
            const PoseFrameKey presented{1000 - clock * quarters / 4, 1, 0};
            REQUIRE(latch.publish(1, clock, presented) == presented);
            const PoseFrameKey queued{1000 - (clock + 1) * quarters / 4, 1, 0};
            CHECK(latch.snapshot(1) == presented);
            CHECK(latch.publish(1, clock, queued) == presented);
            CHECK(latch.snapshot(1) == presented);
        }
    }
}

namespace {
const WristEffectBinding kStart{{0, 0, 7, 41}, 0x10000};
const WristEffectBinding kLoop{{0, 0, 9, 42}, 0x20000};

struct NativeSlots {
    bool (*valid)(const WristMatrixProvider*, const void*);
    void (*copy)(WristMatrixProvider*, void*, const void*);
};
NativeSlots slots(const WristMatrixDescriptor& descriptor) {
    const void* vtable = nullptr;
    std::memcpy(&vtable, descriptor.object, sizeof(vtable));
    NativeSlots out{};
    std::memcpy(&out, vtable, sizeof(out));
    return out;
}
}

TEST_CASE("wrist ownership rejects foreign events and recycled native event slots") {
    WristEffectOwners owners;
    const WristEffectBinding bindings[]{kStart, kLoop};
    const auto serial = owners.publish(11, bindings);
    REQUIRE(serial != 0);
    auto matched = owners.match(kStart.event, kStart.handle.eventId);
    REQUIRE(matched);
    CHECK(matched.serial == serial);
    CHECK(matched.historyGeneration == 11);
    CHECK(owners.match(kLoop.event, kLoop.handle.eventId));
    CHECK_FALSE(owners.match(kStart.event, kLoop.handle.eventId));
    CHECK_FALSE(owners.match(0x30000, kStart.handle.eventId));
    owners.clear();
    CHECK_FALSE(owners.match(kStart.event, kStart.handle.eventId));
    CHECK_FALSE(owners.current(serial));
    auto recycled = kStart;
    ++recycled.handle.eventId;
    const auto next = owners.publish(11, {&recycled, 1});
    CHECK(next > serial);
    CHECK_FALSE(owners.current(serial));
    CHECK_FALSE(owners.match(kStart.event, kStart.handle.eventId));
    CHECK(owners.match(recycled.event, recycled.handle.eventId));
    CHECK_FALSE(owners.match(kLoop.event, kLoop.handle.eventId));
}

TEST_CASE("native wrist provider preserves all recorded rotation and translation values") {
    WristEffectOwners owners;
    const auto serial = owners.publish(7, {&kStart, 1});
    const float matrix[12]{0, -1, 0, 123, 1, 0, 0, -45, 0, 0, 1, 67};
    WristMatrixProvider provider(owners, serial, matrix);
    const auto descriptor = provider.descriptor();
    const auto native = slots(descriptor);
    REQUIRE(native.valid(&provider, &descriptor.serial));
    float out[12]{};
    native.copy(&provider, out, &descriptor.serial);
    CHECK(provider.copied());
    CHECK(std::memcmp(out, matrix, sizeof(out)) == 0);
    auto wrongSerial = descriptor.serial + 1;
    CHECK_FALSE(native.valid(&provider, &wrongSerial));
}

TEST_CASE("effect teardown between native validation and copy leaves the native base intact") {
    WristEffectOwners owners;
    const auto serial = owners.publish(7, {&kStart, 1});
    const float matrix[12]{1, 0, 0, 20, 0, 1, 0, 30, 0, 0, 1, 40};
    WristMatrixProvider provider(owners, serial, matrix);
    const auto descriptor = provider.descriptor();
    const auto native = slots(descriptor);
    REQUIRE(native.valid(&provider, &descriptor.serial));
    owners.clear();
    float out[12]{5, 4, 3, 2};
    const auto before = std::bit_cast<std::array<float, 12>>(out);
    native.copy(&provider, out, &descriptor.serial);
    CHECK_FALSE(provider.copied());
    CHECK(std::memcmp(out, before.data(), sizeof(out)) == 0);
    CHECK_FALSE(native.valid(&provider, &descriptor.serial));
    (void)owners.publish(7, {&kStart, 1});
    CHECK_FALSE(native.valid(&provider, &descriptor.serial));
}

TEST_CASE("invalid wrist transforms and malformed bindings cannot override native effects") {
    WristEffectOwners owners;
    auto invalid = kStart;
    invalid.handle.poolIndex = -1;
    (void)owners.publish(7, {&invalid, 1});
    CHECK_FALSE(owners.match(invalid.event, invalid.handle.eventId));
    invalid = kStart;
    invalid.handle.type = 0xFF;
    (void)owners.publish(7, {&invalid, 1});
    CHECK_FALSE(owners.match(invalid.event, invalid.handle.eventId));
    const auto serial = owners.publish(7, {&kStart, 1});
    float matrix[12]{};
    matrix[11] = std::numeric_limits<float>::quiet_NaN();
    WristMatrixProvider provider(owners, serial, matrix);
    const auto descriptor = provider.descriptor();
    CHECK_FALSE(slots(descriptor).valid(&provider, &descriptor.serial));
    CHECK(owners.publish(0, {&kStart, 1}) == 0);
    CHECK_FALSE(owners.current(serial));
}

TEST_CASE("parallel wrist readers never combine owner generations during publication") {
    WristEffectOwners owners;
    std::atomic<bool> done{false};
    std::atomic<bool> mixed{false};
    std::atomic<unsigned> reads{0};
    (void)owners.publish(11, {&kStart, 1});
    std::thread reader([&] {
        do {
            const auto start = owners.match(kStart.event, kStart.handle.eventId);
            const auto loop = owners.match(kLoop.event, kLoop.handle.eventId);
            if ((start && start.historyGeneration != 11) || (loop && loop.historyGeneration != 22))
                mixed.store(true);
            reads.fetch_add(1);
        } while (!done.load());
    });
    for (unsigned i = 0; i < 4000; ++i) {
        (void)owners.publish(22, {&kLoop, 1});
        (void)owners.publish(11, {&kStart, 1});
    }
    done.store(true);
    reader.join();
    CHECK(reads.load() > 0);
    CHECK_FALSE(mixed.load());
}
