// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombPressure.hpp"
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
unsigned checks{};
void check(bool condition, const char *description) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "%s\n", description);
        std::exit(1);
    }
}
void samples() {
    std::uint32_t x{}, y{}, z{}, radius{}, speed{}, obstructed{};
    while (std::cin >> x >> y >> z >> radius >> speed >> obstructed) {
        const auto result = remote_bomb::blast::pressureWave(
            {std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)},
            std::bit_cast<float>(radius), std::bit_cast<float>(speed), obstructed != 0);
        std::cout << std::bit_cast<std::uint32_t>(result.direction.x) << ' '
                  << std::bit_cast<std::uint32_t>(result.direction.y) << ' '
                  << std::bit_cast<std::uint32_t>(result.direction.z) << ' '
                  << std::bit_cast<std::uint32_t>(result.strength) << '\n';
    }
}
void impulseSamples() {
    std::uint32_t x{}, y{}, z{}, area{}, occlusion{}, scale{}, limited{};
    while (std::cin >> x >> y >> z >> area >> occlusion >> scale >> limited) {
        const auto impulse = remote_bomb::blast::pressureImpulse(
            {std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)},
            std::bit_cast<float>(area), std::bit_cast<float>(occlusion), std::bit_cast<float>(scale), limited != 0);
        std::cout << std::bit_cast<std::uint32_t>(impulse.x) << ' '
                  << std::bit_cast<std::uint32_t>(impulse.y) << ' '
                  << std::bit_cast<std::uint32_t>(impulse.z) << '\n';
    }
}
}
int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--samples") {
        samples();
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--impulse-samples") {
        impulseSamples();
        return 0;
    }
    using remote_bomb::blast::pressureWave;
    check(pressureWave({2.f, 0.f, 0.f}, 0.f, 4.f).strength == 0.f, "zero-radius body receives no pressure");
    check(pressureWave({2.f, 0.f, 0.f}, -1.f, 4.f).strength == 0.f, "negative radius is excluded");
    check(pressureWave({}, .25f, 4.f).strength == 0.f, "coincident point receives no invented direction");
    check(pressureWave({2.f, 0.f, 0.f}, .25f, 4.f, true).strength == 0.f, "obstruction excludes pressure");
    const auto near = pressureWave({.5f, 0.f, 0.f}, .25f, 4.f);
    check(near.strength > 0.f && near.direction.x == 1.f, "nearby target receives outward pressure");
    check(near.strength == pressureWave({1.f, 0.f, 0.f}, .25f, 4.f).strength, "surface distance has a one-metre floor");
    check(pressureWave({2.f, 0.f, 0.f}, .25f, 4.f).strength < near.strength, "pressure weakens with distance");
    check(pressureWave({4.25f, 0.f, 0.f}, .25f, 4.f).strength == 0.f, "original speed threshold reaches zero");
    check(pressureWave({5.f, 0.f, 0.f}, .25f, 4.f).strength == 0.f, "outside pressure range receives no force");
    check(pressureWave({1.f, 0.f, 0.f}, .25f, .5f).strength == 0.f, "sub-threshold speed receives no pressure");
    const auto reverse = pressureWave({-.5f, 0.f, 0.f}, .25f, 4.f);
    check(reverse.strength == near.strength && reverse.direction.x == -1.f, "opposite target has equal outward strength");
    const auto diagonal = pressureWave({2.f, 3.f, -4.f}, 1.f, 15.f);
    check(diagonal.direction.x > 0.f && diagonal.direction.y > 0.f && diagonal.direction.z < 0.f,
          "direction follows all three displacement components");
    const auto tiny = pressureWave({.5f, 0.f, 0.f}, .01f, 4.f);
    check(tiny.strength != near.strength, "effective radius changes pressure coverage");
    using remote_bomb::blast::pressureImpulse;
    check(remote_bomb::blast::incidentVelocity({{1.f, 0.f, 0.f}, 2.f}).x == 680.f,
          "original pressure converts to incident velocity before response");
    const auto impulse = pressureImpulse({340.f, 0.f, 0.f}, 1.f, 1.f, 1.f);
    check(impulse.x == 5780.f, "original response uses squared incident velocity");
    check(impulse.y == 0.f && impulse.z == 0.f, "response remains aligned with incident velocity");
    check(pressureImpulse({-340.f, 0.f, 0.f}, 1.f, 1.f, 1.f).x == -impulse.x,
          "opposite velocity reverses impulse");
    check(pressureImpulse({340.f, 0.f, 0.f}, 2.f, 1.f, 1.f).x == impulse.x * 2.f,
          "doubling projected area doubles impulse");
    check(pressureImpulse({340.f, 0.f, 0.f}, 1.f, .5f, 1.f).x == impulse.x * .5f,
          "shape occlusion attenuates response");
    check(pressureImpulse({340.f, 0.f, 0.f}, 1.f, 1.f, .5f).x == impulse.x * .5f,
          "per-body response scale remains separate");
    check(pressureImpulse({900.f, 0.f, 0.f}, 1.f, 1.f, 1.f, true).x == 18000.f,
          "limited bodies cap incident speed at six hundred");
    check(pressureImpulse({900.f, 0.f, 0.f}, 1.f, 1.f, 1.f).x == 40500.f,
          "unlimited bodies preserve the original stronger response");
    check(pressureImpulse({}, 1.f, 1.f, 1.f).x == 0.f, "zero velocity has no invented response");
    check(pressureImpulse({340.f, 0.f, 0.f}, 1.f, 0.f, 1.f).x == 0.f,
          "fully occluded shapes receive no response");
    const auto background = pressureImpulse({340.f, 0.f, 0.f}, 1.f, 1.f, 1.f, false, {3.f, -4.f, 5.f});
    check(background.x == 5783.f && background.y == -4.f && background.z == 5.f,
          "blast response adds to the existing wind contribution");
    check(pressureImpulse({}, 1.f, 1.f, 1.f, false, {3.f, -4.f, 5.f}).y == -4.f,
          "zero blast retains background response");
    const auto geometry = remote_bomb::blast::targetGeometry(4.f * 0x1.921fb6p+1f / 3.f);
    check(std::fabs(geometry.radius - 1.f) < 1e-6f, "unit sphere volume recovers its radius");
    check(std::fabs(geometry.projectedArea - 0x1.921fb6p+1f) < 1e-6f,
          "sphere response uses projected rather than total surface area");
    std::printf("%u pressure checks passed\n", checks);
}
