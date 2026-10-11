#include "doctest.h"
#include "StasisProjection.hpp"
#include "StasisTarget.hpp"
#include <cstring>
#include <limits>

TEST_CASE("Centre ray follows the displayed camera including translated and rotated views") {
    stasis::render::Camera c;
    c.view[2] = -1;
    c.view[5] = 1;
    c.view[8] = 1;
    c.view[3] = 3;
    c.view[7] = -2;
    c.view[11] = -7;
    c.proj[0] = c.proj[5] = 1;
    c.proj[14] = -1;
    c.width = 1280;
    c.height = 720;
    c.nearMetres = 0.1f;
    c.farMetres = 1000;
    const auto ray = stasis::render::cameraRay(c);
    REQUIRE(ray.valid);
    CHECK(ray.eye.x == 7);
    CHECK(ray.eye.y == 2);
    CHECK(ray.eye.z == 3);
    CHECK(ray.direction.x == -1);
    CHECK(ray.direction.y == 0);
    CHECK(ray.direction.z == 0);
    const auto centre = stasis::render::project(c, {-3, 2, 3});
    CHECK(centre.valid);
    CHECK(centre.x == 640);
    CHECK(centre.y == 360);
    auto moved = ray;
    moved.eye.y += 0.3f;
    CHECK_FALSE(stasis::target::sameRay(ray, moved));
    moved = ray;
    moved.direction = {0, 0, -1};
    CHECK_FALSE(stasis::target::sameRay(ray, moved));
    CHECK(stasis::target::sameRay(ray, ray));
}
TEST_CASE("Borrowed PFX depth is linear and stale selection cannot retain a target ring") {
    stasis::render::Camera c;
    c.view[0] = c.view[5] = c.view[10] = 1;
    c.proj[0] = c.proj[5] = 1;
    c.proj[14] = -1;
    c.width = 1280;
    c.height = 720;
    c.nearMetres = 0.1f;
    c.farMetres = 1000;
    CHECK(stasis::render::sceneDepthMetres(c, 0) == 0.1f);
    CHECK(stasis::render::sceneDepthMetres(c, 1) == doctest::Approx(1000));
    CHECK(stasis::render::sceneDepthMetres(c, 0.5f) == doctest::Approx(500.05f));
    stasis::Presentation s;
    s.selected = true;
    s.position = {0, 0, -10};
    s.direction = {1, 0, 0};
    s.durationFrames = 300;
    s.remainingFrames = 150;
    stasis::render::Uniforms u;
    stasis::render::buildUniforms(c, s, u);
    CHECK(u.flags[3] == 0);
    CHECK(u.selectedMin[3] == 0);
    CHECK(u.timing[0] == 0);
    CHECK(u.tip[3] == 0);
    s.aiming = true;
    stasis::render::buildUniforms(c, s, u);
    CHECK(u.flags[3] == 1);
    CHECK(u.selectedMin[3] == 1);
}
namespace {
unsigned casts{};
std::uint64_t noHit(const void *, const void *, const void *object, const void *group,
                    std::uint32_t mask, std::uint32_t) {
    ++casts;
    CHECK(mask == 44);
    const auto *bytes = static_cast<const unsigned char *>(object);
    CHECK(bytes[0x20] == 0);
    std::uintptr_t body;
    std::memcpy(&body, bytes + 0x170, sizeof(body));
    CHECK(body == 0);
    std::uintptr_t filter;
    std::memcpy(&filter, bytes + 0x60, sizeof(filter));
    CHECK(filter == reinterpret_cast<std::uintptr_t>(object) + 0x128);
    REQUIRE(group != nullptr);
    const auto &exclude = *static_cast<const stasis::target::Group *>(group);
    CHECK(exclude.id == 123);
    CHECK(exclude.enabled == 1);
    return 0;
}
} // namespace
TEST_CASE("A native miss or cancelled generation never becomes a selectable actor") {
    stasis::target::Mailbox mailbox;
    const stasis::Ray ray{{0, 0, 0}, {0, 0, -1}, true};
    alignas(16) unsigned char donor[0x200]{};
    donor[0x20] = 1;
    const auto donorFilter = reinterpret_cast<std::uintptr_t>(donor) + 0x128;
    std::memcpy(donor + 0x60, &donorFilter, sizeof(donorFilter));
    const stasis::target::Group exclude{123, 1, {}};
    std::uintptr_t oldBody = 0x123400;
    std::memcpy(donor + 0x170, &oldBody, sizeof(oldBody));
    stasis::Vec3 near{}, far{100, 0, 0};
    stasis::target::Hit hit;
    casts = 0;
    CHECK(mailbox.begin(ray, near, 1, exclude));
    mailbox.observe(noHit, &far, donor);
    CHECK(casts == 0);
    CHECK(mailbox.busy());
    mailbox.observe(noHit, &near, donor);
    CHECK(casts == 1);
    CHECK(mailbox.poll(hit, 1));
    CHECK_FALSE(hit.hit);
    CHECK(hit.actor == 0);
    CHECK(hit.filterValid);
    CHECK_FALSE(mailbox.busy());
    std::uintptr_t unchanged;
    std::memcpy(&unchanged, donor + 0x60, sizeof(unchanged));
    CHECK(unchanged == donorFilter);
    CHECK(donor[0x20] == 1);
    CHECK(mailbox.begin(ray, near, 2, exclude));
    mailbox.cancel();
    mailbox.observe(noHit, &near, donor);
    CHECK(casts == 1);
    CHECK_FALSE(mailbox.poll(hit, 2));
    CHECK(mailbox.begin(ray, near, 3, exclude));
    mailbox.observe(noHit, &near, donor);
    CHECK(mailbox.poll(hit, 4));
    CHECK(hit.actor == 0);
    CHECK_FALSE(hit.hit);
    CHECK_FALSE(mailbox.begin(ray, near, 5, {}));
    near.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(mailbox.begin(ray, near, 5, exclude));
    near.x = 0;
    CHECK(mailbox.begin(ray, near, 6, exclude));
    stasis::Vec3 nan = near;
    nan.x = std::numeric_limits<float>::quiet_NaN();
    mailbox.observe(noHit, &nan, donor);
    CHECK(casts == 2);
    CHECK(mailbox.busy());
    unchanged = 0;
    std::memcpy(donor + 0x60, &unchanged, sizeof(unchanged));
    mailbox.observe(noHit, &near, donor);
    CHECK(casts == 2);
    CHECK(mailbox.poll(hit, 6));
    CHECK_FALSE(hit.filterValid);
    CHECK_FALSE(hit.hit);
}
namespace {
std::uint64_t bodyHit(const void *, const void *, const void *object, const void *, std::uint32_t,
                      std::uint32_t) {
    auto *bytes = const_cast<unsigned char *>(static_cast<const unsigned char *>(object));
    bytes[0x20] = 1;
    const std::uintptr_t body = 0x123400;
    std::memcpy(bytes + 0x170, &body, sizeof(body));
    return 1;
}
void resolveBody(std::uintptr_t body, stasis::target::Hit &hit) {
    CHECK(body == 0x123400);
    hit.actor = 0x123800;
    hit.name = 0x123c00;
}
} // namespace
TEST_CASE("A ray hit requires an engine-resolved actor rather than interpreting body userdata") {
    stasis::target::Mailbox mailbox;
    const stasis::Ray ray{{0, 0, 0}, {0, 0, -1}, true};
    const stasis::target::Group exclude{123, 1, {}};
    alignas(16) unsigned char donor[0x200]{};
    const auto filter = reinterpret_cast<std::uintptr_t>(donor) + 0x128;
    std::memcpy(donor + 0x60, &filter, sizeof(filter));
    stasis::Vec3 near{};
    stasis::target::Hit hit;
    REQUIRE(mailbox.begin(ray, near, 1, exclude));
    mailbox.observe(bodyHit, &near, donor);
    REQUIRE(mailbox.poll(hit, 1));
    CHECK(hit.hit);
    CHECK(hit.actor == 0);
    CHECK(hit.name == 0);
    REQUIRE(mailbox.begin(ray, near, 2, exclude));
    mailbox.observe(bodyHit, &near, donor, resolveBody);
    REQUIRE(mailbox.poll(hit, 2));
    CHECK(hit.hit);
    CHECK(hit.actor == 0x123800);
    CHECK(hit.name == 0x123c00);
}
