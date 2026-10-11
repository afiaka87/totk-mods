// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativeBlastContact.hpp"
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks{};
void check(bool condition, const char *description) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "%s\n", description);
        std::exit(1);
    }
}
}
int main() {
    namespace native = remote_bomb::engine::explosion;
    std::array<unsigned char, 300> contact;
    contact.fill(0xA5);
    const native::ContactForces background{{3.f, -4.f, 5.f}, {6.f, 7.f, 8.f}};
    native::restoreForces(contact.data(), background);
    const auto captured = native::captureForces(contact.data());
    check(captured.pressure.x == 3.f && captured.pressure.y == -4.f && captured.pressure.z == 5.f,
          "capture preserves unrelated pressure components");
    check(captured.point.x == 6.f && captured.point.y == 7.f && captured.point.z == 8.f,
          "capture preserves the original application point");
    contact[12] = 0x6C;
    const auto withNativeFlags = contact;
    native::restoreForces(contact.data(), {{500.f, 400.f, 300.f}, {9.f, 10.f, 11.f}});
    contact[24] = 0x77;
    native::restoreForces(contact.data(), captured);
    check(native::captureForces(contact.data()).pressure.y == -4.f,
          "removing a private contribution retains other sources' pressure");
    check(contact[12] == 0x6C && contact[24] == 0x77,
          "restoring force does not undo native contact flags or damage state");
    bool canaries = true;
    for (unsigned i = 0; i < contact.size(); ++i)
        if (i < native::kPressureOffset || i >= native::kPressureOffset + sizeof(captured))
            canaries = canaries && (i == 24 || contact[i] == withNativeFlags[i]);
    check(canaries, "force restoration modifies only the verified 24-byte force and point range");
    std::array<unsigned char, 32> result, destination;
    for (unsigned i = 0; i < result.size(); ++i)
        result[i] = static_cast<unsigned char>(i + 10);
    destination.fill(0xCC);
    native::copyShockResult(destination.data(), result.data());
    bool exact = true;
    for (unsigned i = 0; i < native::kShockResultSize; ++i)
        exact = exact && destination[i] == result[i];
    check(exact, "shock result forwards all six floats and both native flags");
    check(destination[26] == 0xCC && destination[27] == 0xCC && destination[31] == 0xCC,
          "shock result forwarding does not overwrite caller padding");
    native::copyShockResult(nullptr, result.data());
    check(destination[31] == 0xCC, "an absent output pointer does not change the source result");
    std::printf("%u blast contact checks passed\n", checks);
}
