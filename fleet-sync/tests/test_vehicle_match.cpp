#include "doctest.h"
#include "pure/VehicleMatch.hpp"
#include <limits>
#include <string>

namespace {
namespace p = linked_stick::pure;
namespace m = p::matched;
p::VehicleShape bike(std::uintptr_t assembly) {
    p::VehicleShape out{};
    out.assembly = assembly;
    out.complete = true;
    out.expected = out.shape.count = 3;
    out.shape.members[0] = {1, {}, m::identity};
    out.shape.members[1] = {2, {0, 0, -2}, m::identity};
    out.shape.members[2] = {2, {0, 0, 2}, m::identity};
    return out;
}
}

TEST_CASE("vehicle admission accepts separate identical builds with reordered members") {
    auto a = bike(1), b = bike(2);
    std::swap(b.shape.members[0], b.shape.members[2]);
    CHECK(p::vehicleMatchFailure(a, b) == nullptr);
    b.shape.members[0].position.x += 0.1f;
    CHECK(p::vehicleMatchFailure(a, b) == nullptr);
}

TEST_CASE("vehicle admission rejects different counts types placements and orientations") {
    const auto a = bike(1);
    auto b = bike(2);
    SUBCASE("count") { b.expected = b.shape.count = 2; }
    SUBCASE("part kind") { b.shape.members[1].kind = 3; }
    SUBCASE("placement") { b.shape.members[1].position.x = 0.3f; }
    SUBCASE("orientation") { b.shape.members[1].rotation = {0, 0, 1, 0, 1, 0, -1, 0, 0}; }
    REQUIRE(b.valid());
    CHECK(std::string(p::vehicleMatchFailure(a, b)) == "different_vehicle_build");
}

TEST_CASE("vehicle admission fails closed for loose sticks incomplete scans and corrupt geometry") {
    const auto a = bike(1);
    auto b = bike(2);
    SUBCASE("loose stick") { b.expected = b.shape.count = 1; }
    SUBCASE("no assembly") { b.assembly = 0; }
    SUBCASE("incomplete roster") { b.complete = false; }
    SUBCASE("missing member") { b.shape.count = 2; }
    SUBCASE("extra member") { b.expected = 2; }
    SUBCASE("oversized") { b.expected = 22; }
    SUBCASE("invalid position") { b.shape.members[2].position.y = std::numeric_limits<float>::quiet_NaN(); }
    SUBCASE("invalid rotation") { b.shape.members[2].rotation = {}; }
    CHECK_FALSE(b.valid());
    CHECK(std::string(p::vehicleMatchFailure(a, b)) == "receiver_vehicle_unavailable");
    CHECK(std::string(p::vehicleMatchFailure(b, a)) == "controller_vehicle_unavailable");
}

TEST_CASE("vehicle admission rejects two sticks on the same physical construction") {
    const auto a = bike(1), b = bike(1);
    CHECK(std::string(p::vehicleMatchFailure(a, b)) == "same_physical_vehicle");
}

TEST_CASE("vehicle shape ignores world heading position and part-name allocation") {
    auto a = bike(1), b = bike(2);
    const m::Mat heading{0, 0, 1, 0, 1, 0, -1, 0, 0};
    const m::Vec origin{100, 20, -50};
    for (unsigned i = 0; i < b.shape.count; ++i) {
        auto& part = b.shape.members[i];
        const auto world = m::add(origin, m::rotate(heading, part.position));
        part.position = m::rotate(m::transpose(heading), m::sub(world, origin));
        part.rotation = m::product(m::transpose(heading), m::product(heading, part.rotation));
    }
    CHECK(p::vehicleMatchFailure(a, b) == nullptr);
    const std::string nameA = "part-a", nameB = "part-a";
    CHECK(p::vehiclePartKind(nameA.c_str()) == p::vehiclePartKind(nameB.c_str()));
    CHECK(p::vehiclePartKind("part-a") != p::vehiclePartKind("part-b"));
}
