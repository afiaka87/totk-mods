#include <doctest.h>
#include <array>
#include <cstring>
#include <limits>

#include "RemoteTargeting.hpp"
#include "Module.hpp"
#include "RemoteProfile121.hpp"

using namespace zonai_ascend::remote;
namespace {
constexpr Vec3 origin{50.25f, 200.0f, 1150.25f};
// Actual clean GSI voxel pair, stage-zonai-ascend 2026-10-31; floats add native voxel-center 0.5.
constexpr Layers gsi{1, 50.5f, 1150.5f, 1469.5f, 1524.5f, kNaN};
template<class T> void put(void* bytes, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(bytes)+offset, &value, sizeof(value));
}
template<class T> T get(const void* bytes, std::size_t offset) {
    T value{}; std::memcpy(&value, static_cast<const std::byte*>(bytes)+offset, sizeof(value));
    return value;
}
struct Fixture {
    std::array<std::byte, 0x300> actor{};
    std::array<std::byte, 0x80> manager{};
    std::array<std::byte, 0x20> query{};
    std::array<std::byte, 0xA0> node{};
    std::array<std::byte, 0x1400> player{};
    std::array<std::byte, 0x80> selector{};
    Layers sample = gsi;
    bool queryOk = true, blocked = false, underground = false, floorOk = true;
    int queries = 0, snaps = 0;
    Vec3 target{};
    static inline Fixture* self;
    Fixture() {
        self = this;
        put(actor.data(), 0x2B4, origin);
        put(manager.data(), 8, 1u);
        put(query.data(), 0, static_cast<void*>(actor.data()));
        Bindings b{};
        b.queryLayers = [](Layers* out, const Vec3*) {
            CHECK_FALSE(finite(out->floor)); // Never reuse a preceding successful sample.
            ++self->queries; *out = self->sample; return self->queryOk;
        };
        b.blockedExit = [](void*, const Vec3*) { return self->blocked; };
        b.isUnderground = [](void*) { return self->underground; };
        b.getActor = [](void*) -> void* { return self->actor.data(); };
        b.getPlayer = [](void*) -> void* { return self->player.data(); };
        b.getSelectorChild = [](void*) -> void* { return self->node.data(); };
        b.snapFloor = [](void*, const Vec3* in, Vec3* out) {
            ++self->snaps; *out = *in; out->y -= 0.05f; return self->floorOk;
        };
        bind(b);
    }
    bool acceptFallback(bool confirm = true) {
        bool failed = true;
        const bool result = resolveFloor(manager.data(), query.data(), &origin, &target, &failed, false);
        if (result) {
            CHECK_FALSE(failed);
            put(manager.data(), 0x18, target);
            put(manager.data(), 0x7F, static_cast<std::uint8_t>(get<std::uint8_t>(manager.data(), 0x7F)|1));
            put(node.data(), 0x3C, target);
            put(node.data(), 0x24, Vec3{origin.x, gsi.ceiling, origin.z});
            put(node.data(), 0x20, 2u);
            if (confirm) {
                put(manager.data(), 0x24, target);
                commit(manager.data());
            }
        }
        return result;
    }
    void pendingApproachCancel() {
        put(selector.data(), 0x18, std::uint8_t{2});
        put(selector.data(), 0x1E, std::uint8_t{3});
        put(selector.data(), 0x70, std::uint8_t{3});
        put(selector.data(), 0x71, std::uint8_t{4});
        put(selector.data(), 0x5F, std::uint8_t{1});
        put(player.data(), 0x132C, target);
        put(player.data(), 0x1344, origin);
        put(node.data(), 0x24, origin);
        put(node.data(), 0x20, 1u);
        put(actor.data(), 0x2B4, Vec3{origin.x, 700, origin.z});
    }
};
}

TEST_CASE("GSI voxel pair yields a bounded island destination") {
    Candidate candidate{};
    REQUIRE(makeCandidate(true, gsi, origin, candidate) == Verdict::Accepted);
    CHECK(candidate.entry.y == 1469.5f);
    CHECK(candidate.exit.y == doctest::Approx(1524.55f));
    CHECK(candidate.exit.x == 50.5f);
}

TEST_CASE("unit availability or a partial query cannot admit a destination") {
    Candidate candidate{};
    CHECK(makeCandidate(false, gsi, origin, candidate) == Verdict::Unavailable);
    Layers partial = gsi; partial.floor = kNaN;
    CHECK(makeCandidate(true, partial, origin, candidate) == Verdict::Incomplete);
    partial = gsi; partial.flags = 0;
    CHECK(makeCandidate(true, partial, origin, candidate) == Verdict::Unavailable);
    partial = gsi; partial.floor = std::numeric_limits<float>::infinity();
    CHECK(makeCandidate(true, partial, origin, candidate) == Verdict::Incomplete);
}

TEST_CASE("stale columns inverted layers and inadequate headroom are refused") {
    Candidate candidate{};
    Layers sample = gsi; sample.x += 1;
    CHECK(makeCandidate(true, sample, origin, candidate) == Verdict::ColumnMismatch);
    sample = gsi; sample.ceiling = origin.y;
    CHECK(makeCandidate(true, sample, origin, candidate) == Verdict::Ordering);
    sample = gsi; sample.floor = sample.ceiling;
    CHECK(makeCandidate(true, sample, origin, candidate) == Verdict::Ordering);
    sample = gsi; sample.nextCeiling = sample.floor+1.99f;
    CHECK(makeCandidate(true, sample, origin, candidate) == Verdict::Headroom);
    sample.nextCeiling = sample.floor+2.0f;
    CHECK(makeCandidate(true, sample, origin, candidate) == Verdict::Accepted);
    sample.nextCeiling = -std::numeric_limits<float>::infinity();
    CHECK(makeCandidate(true, sample, origin, candidate) == Verdict::Headroom);
}

TEST_CASE("negative coordinates and high islands retain the native grid and range") {
    Candidate candidate{};
    Layers sample{1, -0.5f, -1150.5f, 3100.5f, 3200.5f, kNaN};
    CHECK(makeCandidate(true, sample, {-0.01f, 100, -1150.01f}, candidate) == Verdict::Accepted);
    sample.floor = 10100.5f;
    CHECK(makeCandidate(true, sample, {-0.01f, 100, -1150.01f}, candidate) == Verdict::Reach);
}

TEST_CASE("fallback cannot waive dynamic obstruction path or unknown failures") {
    for (auto reason : {0x40, 0x100, 0x1000, 0x4000, 0x8000, 0x1})
        CHECK_FALSE(mayReplaceCeilingFailure(static_cast<std::uint16_t>(reason)));
    CHECK(mayReplaceCeilingFailure(0xC0));
    CHECK(mayReplaceCeilingFailure(0x50));
    CHECK(mayReplaceCeilingFailure(0x60));
    CHECK(mayReplaceFloorFailure(0x2080));
    CHECK_FALSE(mayReplaceFloorFailure(0x2081));
}

TEST_CASE("native success does not consult or replace voxel terrain") {
    Fixture f;
    Vec3 out{1, 2, 3}; bool failed = false;
    CHECK(resolveFloor(f.manager.data(), f.query.data(), &origin, &out, &failed, true));
    CHECK(near(out, {1, 2, 3}, 0));
    CHECK(f.queries == 0);
}

TEST_CASE("floor fallback refreshes after provisional ceiling admission") {
    Fixture f;
    CHECK(tryCeiling(f.manager.data(), f.query.data(), 0xC0));
    f.queryOk = false;
    CHECK_FALSE(f.acceptFallback());
    CHECK(f.queries == 2);
    CHECK_FALSE(cancelUnsafeArrival(f.node.data()));
}

TEST_CASE("initial no-hit reasons do not leak into the later floor decision") {
    Fixture f;
    zonai_ascend::init(0, true, false, 0, 0x2B4);
    put(f.manager.data(), 0x7C, std::uint16_t{0x50});
    REQUIRE(zonai_ascend::resolveQueryValid(f.manager.data(), f.query.data(), false));
    CHECK(get<std::uint16_t>(f.manager.data(), 0x7C) == 0x40);
    put(f.manager.data(), 0x7C, std::uint16_t{0}); // Native update clears its baseline bit.
    CHECK(f.acceptFallback());
}

TEST_CASE("blocked exits and failed native clearance remain refused") {
    Fixture f;
    f.blocked = true;
    CHECK_FALSE(f.acceptFallback());
    f.blocked = false;
    put(f.manager.data(), 0x7C, std::uint16_t{0x8000});
    CHECK_FALSE(f.acceptFallback());
    CHECK(f.queries == 1);
}

TEST_CASE("marker maps only the current accepted upward target") {
    Fixture f;
    REQUIRE(f.acceptFallback());
    Vec3 nativeMarker{0, 3400, 0};
    const void* position = markerPosition(f.manager.data(), f.actor.data(), &nativeMarker);
    REQUIRE(position != &nativeMarker);
    CHECK(static_cast<const Vec3*>(position)->y == gsi.ceiling);
    put(f.manager.data(), 0x18, Vec3{50, 100, 1150}); // Descend replaced pending target.
    CHECK(markerPosition(f.manager.data(), f.actor.data(), &nativeMarker) == &nativeMarker);
}

TEST_CASE("sky routes retain the ordinary event and Depths routes retain the underground event") {
    Fixture sky;
    REQUIRE(sky.acceptFallback());
    CHECK((get<std::uint8_t>(sky.manager.data(), 0x7F) & 4) == 0);
    Fixture depths;
    depths.underground = true;
    REQUIRE(depths.acceptFallback());
    CHECK((get<std::uint8_t>(depths.manager.data(), 0x7F) & 4) != 0);
}

TEST_CASE("missing arrival floor cancels an owned upward event") {
    Fixture f;
    REQUIRE(f.acceptFallback());
    put(f.manager.data(), 8, 0u);
    beginQuery(f.manager.data()); // Background queries after leaving aim must not erase ownership.
    f.floorOk = false;
    CHECK(cancelUnsafeArrival(f.node.data()));
    CHECK((get<std::uint32_t>(f.player.data(), 0xAE4) & 0x80000) != 0);
    CHECK_FALSE(cancelUnsafeArrival(f.node.data()));
    CHECK(f.snaps == 1);
}

TEST_CASE("an aimed but uncommitted target cannot own an event") {
    Fixture f;
    REQUIRE(f.acceptFallback(false));
    f.floorOk = false;
    CHECK_FALSE(cancelUnsafeArrival(f.node.data()));
    CHECK(f.snaps == 0);
}

TEST_CASE("loading is allowed to finish before the floor check") {
    Fixture f;
    REQUIRE(f.acceptFallback());
    put(f.node.data(), 0x20, 1u);
    CHECK_FALSE(cancelUnsafeArrival(f.node.data()));
    CHECK(f.snaps == 0);
    put(f.node.data(), 0x20, 2u);
    CHECK_FALSE(cancelUnsafeArrival(f.node.data()));
    CHECK_FALSE(cancelUnsafeArrival(f.node.data()));
    CHECK(f.snaps == 1);
}

TEST_CASE("arrival check does not own Descend or unrelated targets") {
    Fixture f;
    REQUIRE(f.acceptFallback());
    f.floorOk = false;
    put(f.node.data(), 0x24, Vec3{50, 1800, 1150});
    CHECK_FALSE(cancelUnsafeArrival(f.node.data()));
    CHECK(f.snaps == 0);
    put(f.node.data(), 0x24, origin);
    put(f.node.data(), 0x3C, Vec3{500, 1000, 1150});
    CHECK_FALSE(cancelUnsafeArrival(f.node.data()));
    CHECK(f.snaps == 0);
}

TEST_CASE("new aim discards preceding event ownership and disabled profiles remain native") {
    Fixture f;
    REQUIRE(f.acceptFallback());
    beginQuery(f.manager.data());
    f.floorOk = false;
    CHECK_FALSE(cancelUnsafeArrival(f.node.data()));
    bind({});
    CHECK_FALSE(f.acceptFallback());
    CHECK_FALSE(tryCeiling(f.manager.data(), f.query.data(), 0xC0));
}

TEST_CASE("remote guard requires every instruction within the selected image") {
    auto read = [](std::ptrdiff_t at) {
        for (auto word : profile121::kGuards) if (word.offset == at) return word.original;
        return 0u;
    };
    CHECK(profile121::matches(0x2B19A50, read));
    CHECK_FALSE(profile121::matches(0x100000, read));
    for (const auto& changed : profile121::kGuards)
        CHECK_FALSE(profile121::matches(0x2B19A50, [&](std::ptrdiff_t at) {
            return read(at) ^ (at == changed.offset ? 1u : 0u);
        }));
}

TEST_CASE("B latched in open air requests native return before reaching the island") {
    Fixture f;
    REQUIRE(f.acceptFallback());
    f.pendingApproachCancel();
    put(f.player.data(), 0xAE4, 0x80u);
    put(f.player.data(), 0xAEC, 0x50u);
    finishApproachCancel(f.selector.data());
    CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0x180080u);
    CHECK(get<std::uint32_t>(f.player.data(), 0xAEC) == 0x40u);
    CHECK(get<std::uint8_t>(f.selector.data(), 0x5F) == 0);
    CHECK(near(get<Vec3>(f.player.data(), 0x1344), origin, 0));
    f.floorOk = false;
    CHECK_FALSE(cancelUnsafeArrival(f.node.data()));
    CHECK(f.snaps == 0);
}

TEST_CASE("no B press and uncommitted aims cannot trigger an approach return") {
    Fixture f;
    REQUIRE(f.acceptFallback(false));
    f.pendingApproachCancel();
    finishApproachCancel(f.selector.data());
    CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0);
    put(f.manager.data(), 0x24, f.target);
    commit(f.manager.data());
    put(f.selector.data(), 0x5F, std::uint8_t{0});
    finishApproachCancel(f.selector.data());
    CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0);
}

TEST_CASE("approach cancellation waits for startup but accepts the ceiling contact phase") {
    Fixture f;
    REQUIRE(f.acceptFallback());
    f.pendingApproachCancel();
    put(f.node.data(), 0x20, 0u);
    finishApproachCancel(f.selector.data());
    CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0);
    CHECK(get<std::uint8_t>(f.selector.data(), 0x5F) == 1);
    put(f.node.data(), 0x20, 2u);
    finishApproachCancel(f.selector.data());
    CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0x180000u);
}

TEST_CASE("main travel preview and finished approach retain native cancellation") {
    Fixture f;
    REQUIRE(f.acceptFallback());
    f.pendingApproachCancel();
    put(f.selector.data(), 0x1E, std::uint8_t{4});
    finishApproachCancel(f.selector.data());
    CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0);
    put(f.selector.data(), 0x1E, std::uint8_t{3});
    put(f.node.data(), 0x20, 3u);
    finishApproachCancel(f.selector.data());
    CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0);
}

TEST_CASE("Descend and stale destination ownership cannot accelerate cancellation") {
    Fixture f;
    REQUIRE(f.acceptFallback());
    f.pendingApproachCancel();
    put(f.player.data(), 0x132C, Vec3{50, 100, 1150});
    finishApproachCancel(f.selector.data());
    CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0);
    put(f.player.data(), 0x132C, f.target);
    put(f.node.data(), 0x24, Vec3{50, 1800, 1150});
    finishApproachCancel(f.selector.data());
    CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0);
    put(f.node.data(), 0x24, origin);
    beginQuery(f.manager.data());
    finishApproachCancel(f.selector.data());
    CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0);
}

TEST_CASE("an absent or different return origin leaves cancellation to the game") {
    Fixture f;
    REQUIRE(f.acceptFallback());
    f.pendingApproachCancel();
    for (auto wrong : {Vec3{kNaN, 200, 1150}, Vec3{50, 600, 1150}, Vec3{0, 0, 0}}) {
        put(f.player.data(), 0x1344, wrong);
        finishApproachCancel(f.selector.data());
        CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0);
        CHECK(get<std::uint8_t>(f.selector.data(), 0x5F) == 1);
    }
    put(f.player.data(), 0x1344, origin);
    put(f.node.data(), 0x24, Vec3{origin.x, 600, origin.z});
    finishApproachCancel(f.selector.data());
    CHECK(get<std::uint32_t>(f.player.data(), 0xAE4) == 0);
}
