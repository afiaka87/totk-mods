// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "doctest.h"
#include "StasisTarget.hpp"
#include <cstring>

namespace {
// Verified Bootup.Nin_NX_NVN.pack.zs Phive/Config/PhiveConfig.byml (1.2.1).
constexpr std::uint32_t groundMask = 0x00c20f04, searchMask = 0x00c20f67;
struct Collider {
    unsigned layer;
    std::uintptr_t body;
};
Collider scene[3]{};
std::uint64_t cast(const void *, const void *, const void *object, const void *group,
                   std::uint32_t index, std::uint32_t) {
    REQUIRE((index == 32 || index == 44));
    REQUIRE(group != nullptr);
    const auto &exclude = *static_cast<const stasis::target::Group *>(group);
    CHECK(exclude.id == 123);
    CHECK(exclude.enabled == 1);
    auto *bytes = const_cast<unsigned char *>(static_cast<const unsigned char *>(object));
    const auto mask = index == 32 ? groundMask : searchMask;
    for (const auto c : scene)
        if (c.body && (mask & (1u << c.layer))) {
            bytes[0x20] = 1;
            std::memcpy(bytes + 0x170, &c.body, sizeof(c.body));
            return 1;
        }
    return 0;
}
void resolve(std::uintptr_t body, stasis::target::Hit &hit) {
    // Terrain has no actor; preserve nearest-hit occlusion.
    if (body != 0x123400) {
        hit.actor = body + 0x400;
        hit.name = body + 0x800;
    }
}
} // namespace
TEST_CASE("Enemy target search includes characters while terrain still occludes") {
    alignas(16) unsigned char donor[0x200]{};
    const auto filter = reinterpret_cast<std::uintptr_t>(donor) + 0x128;
    std::memcpy(donor + 0x60, &filter, sizeof(filter));
    const stasis::Ray ray{{0, 0, 0}, {0, 0, -1}, true};
    const stasis::target::Group exclude{123, 1, {}};
    stasis::Vec3 near{}, end{0, 0, -30};
    stasis::target::Mailbox mailbox;
    stasis::target::Hit hit;

    // Character=5, GroundHighRes=8: the old ray skipped the foreground enemy.
    scene[0] = {5, 0x125000};
    scene[1] = {8, 0x123400};
    scene[2] = {};
    cast(&near, &end, donor, &exclude, 32, 0);
    std::uintptr_t previous{};
    std::memcpy(&previous, donor + 0x170, sizeof(previous));
    REQUIRE(previous == 0x123400);
    REQUIRE(mailbox.begin(ray, near, 1, exclude));
    mailbox.observe(cast, &near, donor, resolve);
    REQUIRE(mailbox.poll(hit, 1));
    CHECK(hit.hit);
    CHECK(hit.actor == 0x125400);

    scene[0] = {8, 0x123400};
    scene[1] = {5, 0x125000};
    REQUIRE(mailbox.begin(ray, near, 2, exclude));
    mailbox.observe(cast, &near, donor, resolve);
    REQUIRE(mailbox.poll(hit, 2));
    CHECK(hit.hit);
    CHECK(hit.actor == 0);

    // Object=0 and SmallObject=1 were absent from the old ground-only mask.
    for (unsigned layer = 0; layer <= 2; ++layer) {
        scene[0] = {layer, 0x126000};
        scene[1] = {};
        REQUIRE(mailbox.begin(ray, near, 3, exclude));
        mailbox.observe(cast, &near, donor, resolve);
        REQUIRE(mailbox.poll(hit, 3));
        CHECK(hit.actor == 0x126400);
    }
    scene[0] = {3, 0x127000}; // Player layer is absent from BowSearchTarget.
    REQUIRE(mailbox.begin(ray, near, 4, exclude));
    mailbox.observe(cast, &near, donor, resolve);
    REQUIRE(mailbox.poll(hit, 4));
    CHECK_FALSE(hit.hit);
    CHECK(hit.actor == 0);
}
