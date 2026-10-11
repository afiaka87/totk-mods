#include "doctest.h"
#include "StasisProjection.hpp"
TEST_CASE("Accepted camera convention projects centre, right, up and rejects behind camera") {
    stasis::render::Camera c;
    c.width = 1280;
    c.height = 720;
    c.nearMetres = 0.1f;
    c.farMetres = 1000;
    c.view[0] = c.view[5] = c.view[10] = 1;
    c.proj[0] = c.proj[5] = 1;
    c.proj[14] = -1;
    const auto centre = stasis::render::project(c, {0, 0, -10});
    CHECK(centre.valid);
    CHECK(centre.x == 640);
    CHECK(centre.y == 360);
    CHECK(centre.z == 10);
    const auto right = stasis::render::project(c, {10, 0, -10});
    CHECK(right.x == 1280);
    const auto up = stasis::render::project(c, {0, 10, -10});
    CHECK(up.y == 0);
    CHECK_FALSE(stasis::render::project(c, {0, 0, 10}).valid);
    CHECK_FALSE(stasis::render::project(c, {0, 0, -0.01f}).valid);
}
TEST_CASE("Approved arrow world extent and screen visibility are retained in render uniforms") {
    stasis::render::Camera c;
    c.width = 1280;
    c.height = 720;
    c.nearMetres = 0.1f;
    c.farMetres = 1000;
    c.view[0] = c.view[5] = c.view[10] = 1;
    c.proj[0] = c.proj[5] = 1;
    c.proj[14] = -1;
    stasis::Presentation s;
    s.position = {0, 0, -10};
    s.arrowOrigin = s.position;
    s.direction = {1, 0, 0};
    s.frozen = true;
    s.durationFrames = 300;
    s.remainingFrames = 150;
    stasis::render::Uniforms low, high;
    stasis::render::buildUniforms(c, s, low);
    s.ratio = 1;
    stasis::render::buildUniforms(c, s, high);
    CHECK(low.tip[3] == 1);
    CHECK(high.tip[3] == 1);
    CHECK(low.tip[0] - low.tail[0] == doctest::Approx(.9f * 64));
    CHECK(high.tip[0] - high.tail[0] == doctest::Approx(6.2f * 64));
    CHECK(low.tail[3] == doctest::Approx(.32f * 64));
    CHECK(high.tail[3] == doctest::Approx(.56f * 64));
    CHECK(high.timing[0] == 0.5f);
    s.enemy = true;
    stasis::render::buildUniforms(c, s, high);
    CHECK(high.tip[3] == 0);
}
TEST_CASE("Arrow uses actor origin and a world basis independently of surface bounds") {
    stasis::render::Camera c;
    c.width = 1280;
    c.height = 720;
    c.nearMetres = .1f;
    c.farMetres = 1000;
    c.view[0] = c.view[5] = c.view[10] = 1;
    c.proj[0] = c.proj[5] = 1;
    c.proj[14] = -1;
    stasis::Presentation s;
    s.position = {0, 0, -10};
    s.arrowOrigin = {2, 3, -10};
    s.direction = {0, 0, -1};
    s.frozen = true;
    s.ratio = .5f;
    stasis::render::Uniforms u;
    stasis::render::buildUniforms(c, s, u);
    CHECK(u.target[0] == 640);
    CHECK(u.tail[0] == 768);
    CHECK(u.tail[1] == 252);
    CHECK(u.tip[2] - u.tail[2] == doctest::Approx(3.55f));
    CHECK(u.availableMin[1][0] == -1);
    CHECK(u.availableMin[2][1] == 1);
    CHECK(u.availableMin[3][2] == -1);
    CHECK(u.availableMin[0][3] * .33875746f == doctest::Approx(.44f));
    CHECK(u.availableMin[1][3] * 2.9643395f == doctest::Approx(3.55f));
}
TEST_CASE("Activation fits a wide flat object and stays finite at the near plane") {
    stasis::render::Camera c;
    c.width = 1600;
    c.height = 900;
    c.nearMetres = .1f;
    c.farMetres = 1000;
    c.view[0] = c.view[5] = c.view[10] = 1;
    c.proj[0] = 1;
    c.proj[5] = 1600.0f / 900;
    c.proj[14] = -1;
    stasis::Presentation s;
    s.position = {1817, 219, -10};
    s.bounds = {{1814.83f, 218.675f, -11.607f}, {1819.17f, 219.325f, -8.393f}};
    c.view[3] = -1817;
    c.view[7] = -219;
    stasis::render::Uniforms u;
    stasis::render::buildUniforms(c, s, u);
    CHECK(u.resources[0] == doctest::Approx(2.17f * 800 / 8.393f).epsilon(.001));
    CHECK(u.resources[1] == doctest::Approx(.325f * 800 / 8.393f).epsilon(.001));
    CHECK(u.resources[0] > 6 * u.resources[1]);
    s.position = {1817, 219, -.15f};
    s.bounds = {{1814.83f, 218.675f, -1}, {1819.17f, 219.325f, .5f}};
    stasis::render::buildUniforms(c, s, u);
    CHECK(u.resources[0] >= 12);
    CHECK(u.resources[0] <= 640);
    CHECK(u.resources[1] >= 12);
    CHECK(u.resources[1] <= 360);
}
