#include <algorithm>
#include <string>
#include <vector>
#include "doctest.h"
#include "pure/ObstaclePassThrough.hpp"
namespace c = linked_stick::pure;
namespace {
struct Backend {
    struct Body { c::CollisionBody key; std::uint32_t mask = 0x03ffffff; };
    std::vector<Body> live;
    unsigned writes = 0, invalidWrites = 0;
    bool reject = false;
    std::vector<std::string> events;
    bool readCollisionMask(c::CollisionBody key, std::uint32_t& value) {
        for (const auto& b : live) if (b.key == key) { value = b.mask; return true; }
        return false;
    }
    bool writeCollisionMask(c::CollisionBody key, std::uint32_t value) {
        for (auto& b : live) if (b.key == key) {
            ++writes;
            if (reject) return false;
            b.mask = value; return true;
        }
        ++invalidWrites; return false;
    }
    void collisionEvent(const char* event, c::CollisionBody, std::uint32_t, std::uint32_t) { events.emplace_back(event); }
};
}
TEST_CASE("selective obstacle mask preserves terrain structures and water") {
    for (const unsigned layer : {2u, 7u, 8u, 9u, 10u, 12u, 18u, 22u})
        CHECK((c::kPassThroughLayers & (1u << layer)) == 0);
    for (const unsigned layer : {0u, 1u, 3u, 4u, 5u, 6u, 11u, 23u, 25u})
        CHECK((c::kPassThroughLayers & (1u << layer)) != 0);
}
TEST_CASE("collision leases restore only removed bits and never modify guide") {
    c::ObstaclePassThrough leases;
    c::ObstaclePassThrough::Bodies desired{};
    const c::CollisionBody receiver{1, 10, 0x1000001}, guide{1, 20, 0x1000002};
    Backend b;
    b.live = {{receiver, 0x03fffffdu}, {guide, 0x03ffffffu}};
    desired[0] = receiver;
    leases.update(1, desired, 1, b);
    CHECK(leases.size() == 1);
    CHECK(b.live[0].mask == (0x03fffffdu & ~c::kPassThroughLayers));
    CHECK(b.live[1].mask == 0x03ffffffu);
    const auto writes = b.writes;
    leases.update(1, desired, 1, b);
    CHECK(b.writes == writes);
    b.live[0].mask &= ~(1u << 14);
    leases.update(1, desired, 0, b);
    CHECK(leases.size() == 1);
    leases.update(1, desired, 0, b);
    CHECK(leases.size() == 0);
    CHECK(b.live[0].mask == (0x03fffffdu & ~(1u << 14)));
    CHECK(b.invalidWrites == 0);
}
TEST_CASE("collision restoration retries refused queue writes") {
    c::ObstaclePassThrough leases;
    c::ObstaclePassThrough::Bodies desired{};
    desired[0] = {1, 10, 0x1000001};
    Backend b; b.live = {{desired[0]}};
    leases.update(1, desired, 1, b);
    b.reject = true;
    leases.update(1, desired, 0, b);
    CHECK(leases.size() == 1);
    b.reject = false;
    leases.update(1, desired, 0, b);
    CHECK(leases.size() == 1);
    leases.update(1, desired, 0, b);
    CHECK(leases.size() == 0);
    CHECK(b.live[0].mask == 0x03ffffffu);
}
TEST_CASE("collision leases retire vanished generations and worlds without stale writes") {
    for (int change = 0; change < 3; ++change) {
        c::ObstaclePassThrough leases;
        c::ObstaclePassThrough::Bodies desired{};
        desired[0] = {1, 10, 0x1000001};
        Backend b; b.live = {{desired[0]}};
        leases.update(1, desired, 1, b);
        const auto before = b.writes;
        if (change == 0) b.live.clear();
        if (change == 1) { ++b.live[0].key.id; b.live[0].mask = 1234; }
        leases.update(change == 2 ? 2u : 1u, desired, 0, b);
        CHECK(b.writes == before);
        CHECK(b.invalidWrites == 0);
        CHECK(leases.size() == 0);
    }
}
TEST_CASE("collision body rebinding restores live old body before admitting replacement") {
    c::ObstaclePassThrough leases;
    c::ObstaclePassThrough::Bodies desired{};
    desired[0] = {1, 10, 0x1000001};
    Backend b; b.live = {{desired[0]}, {{1, 30, 0x1000003}}};
    leases.update(1, desired, 1, b);
    desired[0] = b.live[1].key;
    leases.update(1, desired, 1, b);
    CHECK(b.live[0].mask == 0x03ffffffu);
    CHECK(b.live[1].mask == (0x03ffffffu & ~c::kPassThroughLayers));
    leases.update(1, desired, 1, b);
    CHECK(leases.size() == 1);
    CHECK(b.invalidWrites == 0);
}
TEST_CASE("collision capacity supports all four full constructions and duplicate admission") {
    c::ObstaclePassThrough leases;
    c::ObstaclePassThrough::Bodies desired{};
    Backend b;
    for (unsigned i = 0; i < desired.size(); ++i) { desired[i] = {1, 10u + i, 0x1000001u + i}; b.live.push_back({desired[i]}); }
    leases.update(1, desired, static_cast<unsigned>(desired.size()), b);
    CHECK(leases.size() == 84);
    leases.update(1, desired, 0, b);
    leases.update(1, desired, 0, b);
    CHECK(leases.size() == 0);
    for (const auto& body : b.live) CHECK(body.mask == 0x03ffffffu);
    desired[1] = desired[0];
    leases.update(1, desired, 2, b);
    CHECK(leases.size() == 1);
}
TEST_CASE("collision restoration keeps ownership if an accepted request is overwritten") {
    c::ObstaclePassThrough leases;
    c::ObstaclePassThrough::Bodies desired{};
    desired[0] = {1, 10, 0x1000001};
    Backend b; b.live = {{desired[0]}};
    leases.update(1, desired, 1, b);
    leases.update(1, desired, 0, b);
    REQUIRE(leases.size() == 1);
    b.live[0].mask &= ~c::kPassThroughLayers;
    leases.update(1, desired, 0, b);
    CHECK(leases.size() == 1);
    leases.update(1, desired, 0, b);
    CHECK(leases.size() == 0);
    CHECK(b.live[0].mask == 0x03ffffffu);
}
