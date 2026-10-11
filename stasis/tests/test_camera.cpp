#include "doctest.h"
#include "StasisCamera.hpp"
#include "StasisProjection.hpp"
#include <limits>
#include <cstring>
using namespace stasis;
static camera::View normal() { return {{0, 2, 5}, {0, 2, 0}, {0, 0, 0}, .7854f}; }
TEST_CASE("Aim camera moves Link below left while keeping the final centre ray aligned") {
    camera::Blend blend;
    auto v = normal();
    CHECK(blend.apply(v, 1, 1, true, 3));
    v = normal();
    CHECK(blend.apply(v, 1, 1, true, 3));
    CHECK(v.eye.x == doctest::Approx(.6));
    CHECK(v.eye.y == doctest::Approx(2.65));
    CHECK(v.at.x == doctest::Approx(.6));
    CHECK(v.at.y == doctest::Approx(2.65));
    CHECK(v.eye.z == 5);
    CHECK(v.at.z == 0);
    CHECK(v.fovy == normal().fovy);
    render::Camera c;
    c.width = 2048;
    c.height = 1152;
    c.nearMetres = .1f;
    c.farMetres = 1000;
    c.view[0] = c.view[5] = c.view[10] = 1;
    c.view[3] = -v.eye.x;
    c.view[7] = -v.eye.y;
    c.view[11] = -v.eye.z;
    c.proj[0] = 1.357995f;
    c.proj[5] = 2.414213f;
    c.proj[14] = -1;
    const auto point = render::project(c, v.at);
    CHECK(point.valid);
    CHECK(point.x == doctest::Approx(1024));
    CHECK(point.y == doctest::Approx(576));
    const auto link = render::project(c, normal().at);
    CHECK(link.x < point.x);
    CHECK(link.y > point.y);
    const auto ray = render::cameraRay(c);
    CHECK(ray.valid);
    CHECK(ray.eye.x == doctest::Approx(v.eye.x));
    CHECK(ray.direction.x == 0);
    CHECK(ray.direction.y == 0);
    CHECK(ray.direction.z == -1);
}
TEST_CASE("Camera framing follows orbit and steep pitch without changing native view direction") {
    for (const auto v0 : {camera::View{{5, 2, 0}, {0, 2, 0}, {0, 1, 0}, .7854f},
                          camera::View{{0, 7, .1f}, {0, 2, 0}, {0, 1, 0}, .7854f}}) {
        auto v = v0;
        camera::Blend blend;
        blend.apply(v, 1, 1, true, 3);
        v = v0;
        blend.apply(v, 1, 1, true, 3);
        const Vec3 before{v0.at.x - v0.eye.x, v0.at.y - v0.eye.y, v0.at.z - v0.eye.z};
        CHECK(v.at.x - v.eye.x == doctest::Approx(before.x));
        CHECK(v.at.y - v.eye.y == doctest::Approx(before.y));
        CHECK(v.at.z - v.eye.z == doctest::Approx(before.z));
        CHECK(finite(v.eye));
        CHECK(finite(v.at));
        if (v0.eye.x > 0)
            CHECK(v.eye.z < 0);
    }
}
TEST_CASE("Camera eases out exactly and clears offsets on scene change") {
    camera::Blend blend;
    auto v = normal();
    blend.apply(v, 1, 1, true, 3);
    v = normal();
    blend.apply(v, 1, 1, true, 3);
    CHECK(blend.amount == 1);
    v = normal();
    blend.apply(v, 1, 1, false, 0);
    CHECK(blend.amount == 1);
    v = normal();
    blend.apply(v, 1, 1, false, 3);
    CHECK(blend.amount == .5f);
    v = normal();
    blend.apply(v, 1, 1, false, 3);
    const auto baseline = normal();
    CHECK(blend.amount == 0);
    CHECK(std::memcmp(&v, &baseline, sizeof(v)) == 0);
    blend.apply(v, 1, 1, true, 3);
    v = normal();
    blend.apply(v, 1, 2, false, 1);
    CHECK(blend.amount == 0);
    CHECK(std::memcmp(&v, &baseline, sizeof(v)) == 0);
    blend.apply(v, 1, 2, true, 3);
    v = normal();
    blend.apply(v, 2, 2, false, 1);
    CHECK(blend.amount == 0);
}
TEST_CASE("Invalid camera input cannot contaminate native output") {
    for (unsigned bad = 0; bad < 5; ++bad) {
        camera::Blend blend;
        auto v = normal();
        if (bad == 0)
            v.at = v.eye;
        if (bad == 1)
            v.at = {0, 3, 5};
        if (bad == 2)
            v.eye.x = std::numeric_limits<float>::infinity();
        if (bad == 3)
            v.fovy = std::numeric_limits<float>::quiet_NaN();
        if (bad == 4)
            v.fovy = 4;
        const auto before = v;
        CHECK_FALSE(blend.apply(v, 1, 1, true, 1));
        CHECK(std::memcmp(&v, &before, sizeof(v)) == 0);
        CHECK(blend.amount == 0);
    }
    camera::Blend blend;
    auto v = normal();
    blend.apply(v, 1, 1, true, -1);
    CHECK(blend.amount == 0);
    blend.apply(v, 1, 1, true, std::numeric_limits<float>::quiet_NaN());
    CHECK(blend.amount == 0);
}
TEST_CASE("Camera transition has the same duration at 30 and 60 fps and never accumulates") {
    camera::Blend slow, fast;
    auto a = normal(), b = normal();
    for (unsigned i = 0; i < 6; ++i) {
        a = normal();
        slow.apply(a, 1, 1, true, 1);
    }
    for (unsigned i = 0; i < 12; ++i) {
        b = normal();
        fast.apply(b, 1, 1, true, .5f);
    }
    CHECK(a.eye.x == doctest::Approx(b.eye.x));
    CHECK(a.eye.y == doctest::Approx(b.eye.y));
    CHECK(slow.amount == doctest::Approx(1));
    CHECK(fast.amount == doctest::Approx(1));
    for (unsigned i = 0; i < 300; ++i) {
        a = normal();
        a.auxiliary = {7, 8, 9};
        slow.apply(a, 1, 1, true, 1);
    }
    CHECK(a.eye.x == doctest::Approx(.6));
    CHECK(a.eye.y == doctest::Approx(2.65));
    CHECK(a.auxiliary.x == 7);
    CHECK(a.auxiliary.y == 8);
    CHECK(a.auxiliary.z == 9);
    slow.apply(a, 1, 0, true, 1);
    CHECK(slow.amount == 0);
}
