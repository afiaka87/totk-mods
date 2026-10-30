#include "doctest.h"
#include "StasisHit.hpp"
#include "StasisProjection.hpp"
#include <array>
#include <cstring>
TEST_CASE("Unarmed Player hits use Link's position without reading a missing weapon owner") {
    const std::uintptr_t player = 0x10000, owner = 0x20000;
    const auto source = stasis::hit_adapter::strikeOriginActor(player, 0);
    REQUIRE(source == player);
    CHECK(stasis::hit_adapter::strikeOriginActor(0x30000, owner) == owner);
    const auto direction = stasis::hit_adapter::playerDirection({1, 0, 0}, {0, 0, 0}, {0, 0, 4});
    stasis::ChargeState charge;
    stasis::acceptStrike(charge, {direction, {}, -1, true, true, true}, 100, true);
    CHECK(charge.charged);
    CHECK(charge.counter == 1);
    CHECK(charge.vector.z > 0);
    CHECK(std::isfinite(charge.vector.y));
}

TEST_CASE("Projectile charge follows travel instead of becoming a raised player strike") {
    const stasis::Vec3 force{0, 2, 0}, velocity{3, -4, 0};
    const auto direction = stasis::hit_adapter::projectileDirection(force, velocity);
    CHECK(direction.x == doctest::Approx(1.2f));
    CHECK(direction.y == doctest::Approx(-1.6f));
    CHECK(stasis::length(direction) == doctest::Approx(2));
    stasis::ChargeState projectile, player;
    stasis::acceptStrike(projectile, {direction, {}, -1, false, true, true}, 1, true);
    stasis::acceptStrike(player, {direction, {}, 0, true, true, true}, 1, true);
    CHECK(projectile.counter == 0);
    CHECK(projectile.vector.y < 0);
    CHECK(player.counter == 1);
    CHECK(player.vector.y > 0);
    CHECK(stasis::hit_adapter::projectileDirection(force, {}).y == 2);
    CHECK(stasis::hit_adapter::projectileDirection(force, {NAN, 0, 0}).y == 2);
}
TEST_CASE("Unarmed contacts charge heavy objects without depending on a mod attack name or force") {
    for (const float mass : {1.0f, 1000.0f}) {
        for (const stasis::Vec3 force : {stasis::Vec3{}, stasis::Vec3{0, 0, 100}}) {
            stasis::ChargeState charge;
            const auto kind = stasis::hit_adapter::chargeWeaponKind(-1, true);
            const auto direction = stasis::hit_adapter::unarmedDirection(force, {}, {-4, 0, 0});
            stasis::acceptStrike(charge, {direction, {}, kind, true, true, true}, mass, true);
            CHECK(charge.charged);
            CHECK(stasis::length(charge.vector) / mass == doctest::Approx(7.5f));
            CHECK(charge.vector.x < 0);
            CHECK(charge.vector.z == doctest::Approx(0));
        }
    }
    for (const int kind : {-1, 0, 1, 2})
        CHECK(stasis::hit_adapter::chargeWeaponKind(kind, false) == kind);
    CHECK_FALSE(stasis::finite(stasis::hit_adapter::unarmedDirection({NAN, 0, 0}, {}, {})));
}
TEST_CASE("Player redirection matches the original ARM impulse block") {
    struct Case {
        stasis::Vec3 force, player, centre, expected;
    };
    // Independent ARM execution with only the center-of-mass getter stubbed.
    const Case cases[]{
        {{.3f, -.95f, .08f},
         {10, 30, 5},
         {13, 80, 9},
         {.18629010021686554f, -.949999988079071f, .24838678538799286f}},
        {{1, 2, 3}, {0, 0, 0}, {-3, 40, 4}, {-1.8973666429519653f, 2, 2.5298221111297607f}},
        {{0, -1, 0}, {0, 0, 0}, {2, 0, 2}, {0, -1, 0}},
        {{1, 0, 2}, {1, 0, 1}, {1, 8, 1}, {1, 0, 2}},
        {{-.7f, .2f, .5f},
         {-4, 8, -5},
         {-4.01f, 1, -4.99f},
         {-.6082762479782104f, .20000000298023224f, .6082762479782104f}},
        {{.003f, -.99f, .0001f},
         {6000, 90, -3000},
         {6003, 50, -3002},
         {.0024975372944027185f, -.9900000095367432f, -.0016650247853249311f}}};
    for (const auto &c : cases) {
        const auto out = stasis::hit_adapter::playerDirection(c.force, c.player, c.centre);
        CHECK(out.x == doctest::Approx(c.expected.x).epsilon(2.4e-7));
        CHECK(out.y == c.expected.y);
        CHECK(out.z == doctest::Approx(c.expected.z).epsilon(2.4e-7));
    }
}
TEST_CASE("Native hit component link differs from its vtable; independent record fields survive "
          "decoding") {
    // Independent raw layout from TotK 1.2.1 0x8638B8 and 0x14DEFB8, not a Payload copy.
    std::array<unsigned char, 160> record{};
    const float normal[]{0.25f, -0.5f, 0.75f}, point[]{101.0f, 202.0f, 303.0f};
    std::memcpy(record.data() + 12, normal, 12);
    std::memcpy(record.data() + 64, point, 12);
    const std::uint32_t ids[]{0xAABBCCDD, 0x10203040, 0x50607080, 0xF0E0D0C0};
    std::memcpy(record.data() + 56, ids, 8);
    std::memcpy(record.data() + 152, ids + 2, 8);
    const auto p = stasis::hit_adapter::decode(
        reinterpret_cast<std::uintptr_t>(record.data()), []<class T>(std::uintptr_t address) {
            T value;
            std::memcpy(&value, reinterpret_cast<void *>(address), sizeof(T));
            return value;
        });
    CHECK(p.normal.x == 0.25f);
    CHECK(p.normal.y == -0.5f);
    CHECK(p.normal.z == 0.75f);
    CHECK(p.point.x == 101);
    CHECK(p.point.y == 202);
    CHECK(p.point.z == 303);
    for (unsigned i = 0; i < 4; ++i)
        CHECK(p.attack[i] == ids[i]);
    std::array<std::uintptr_t, 5> component{0x12345678, 0, 0, 0x87654321, 0};
    const auto link =
        stasis::hit_adapter::actorLink(reinterpret_cast<std::uintptr_t>(component.data()));
    CHECK(link == reinterpret_cast<std::uintptr_t>(&component[3]));
    CHECK(*reinterpret_cast<const std::uintptr_t *>(link) == 0x87654321);
    CHECK(*reinterpret_cast<const std::uintptr_t *>(link) != component[0]);
    CHECK(stasis::hit_adapter::actorLink(0) == 0);
}
TEST_CASE("Six world chains clip at the camera plane instead of silently vanishing") {
    stasis::render::Camera c;
    c.width = 1280;
    c.height = 720;
    c.nearMetres = .1f;
    c.farMetres = 1000;
    c.view[0] = c.view[5] = c.view[10] = 1;
    c.proj[0] = c.proj[5] = 1;
    c.proj[14] = -1;
    stasis::Presentation p;
    p.position = {0, 0, -3};
    p.frozen = true;
    stasis::render::Uniforms u;
    stasis::render::buildUniforms(c, p, u);
    for (const auto &chain : u.chains) {
        CHECK(chain[3] > 0);
        CHECK(chain[3] <= doctest::Approx(12.0f));
        CHECK(chain[2] >= doctest::Approx(.2f));
        CHECK(std::isfinite(chain[0]));
        CHECK(std::isfinite(chain[1]));
    }
    p.frozen = false;
    stasis::render::buildUniforms(c, p, u);
    for (const auto &chain : u.chains)
        CHECK(chain[3] == 0);
}
