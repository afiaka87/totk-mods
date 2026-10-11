// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BlastWindow.hpp"
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
    using remote_bomb::blast::Admission;
    using remote_bomb::blast::Activation;
    remote_bomb::blast::Windows<2, 2> windows;
    constexpr std::uint64_t halfFrame = std::uint64_t{1} << 31;
    check(windows.admit(0, 10, 0) == Admission::Unavailable, "missing source cannot reserve a force window");
    check(windows.admit(1, 0, 0) == Admission::Unavailable, "missing target cannot reserve a force window");
    check(windows.admit(1, 10, 0) == Admission::Unavailable, "a target contact cannot activate a blast");
    check(windows.activate(0, 0) == Activation::Unavailable, "missing source cannot activate");
    check(windows.activate(1, 0) == Activation::Started, "clock zero is a valid activation step");
    check(windows.admit(1, 10, 0) == Admission::Apply, "an active source can apply its first contact");
    check(windows.admit(1, 10, 0) == Admission::Duplicate, "repeated contacts in one step cannot multiply impulse");
    check(windows.admit(1, 11, 0) == Admission::Apply, "the same blast can affect different bodies");
    check(windows.admit(1, 12, 0) == Admission::Capacity, "overflow is explicit without replacing a visited target");
    check(windows.activate(2, 0) == Activation::Started, "a second bomb has its own force window");
    check(windows.admit(2, 10, 0) == Admission::Apply, "independent blasts may affect the same target");
    check(windows.activate(3, 0) == Activation::Capacity, "source overflow remains explicit");
    check(windows.activate(1, halfFrame) == Activation::Existing, "repeated preparation cannot restart the window");
    check(windows.admit(1, 10, halfFrame) == Admission::Apply, "a half-frame update within the original lifetime can apply force");
    check(windows.admit(1, 10, halfFrame) == Admission::Duplicate, "a stopped clock cannot apply the same contact twice");
    check(windows.admit(1, 10, halfFrame * 2) == Admission::Expired, "one gameplay frame ends the pressure lifetime");
    check(windows.admit(1, 12, halfFrame * 8) == Admission::Expired, "a late contact cannot restart an expired blast");
    check(windows.activate(2, halfFrame * 8) == Activation::Existing, "contact-free source preparation does not restart life");
    check(windows.admit(2, 11, halfFrame * 8) == Admission::Expired, "a new target after expiry gets no fresh impulse");
    windows.reset(1);
    check(windows.admit(1, 10, halfFrame * 8) == Admission::Unavailable, "removed source contacts do not recreate a window");
    check(windows.activate(3, halfFrame * 8) == Activation::Started, "native removal frees a source slot");
    check(windows.admit(3, 10, halfFrame * 8) == Admission::Apply, "a newly prepared source can apply force");
    windows.reset(3);
    check(windows.activate(1, halfFrame * 8) == Activation::Started, "a recreated element may safely reuse its address");
    check(windows.admit(1, 10, halfFrame * 8) == Admission::Apply, "recreated source has a new force window");
    check(windows.admit(1, 10, 0) == Admission::Expired, "clock rewind cannot revive stale force");
    windows = {};
    check(windows.admit(2, 10, 0) == Admission::Unavailable, "scene reset requires a new activation phase");
    check(windows.activate(2, 0) == Activation::Started, "scene reset discards source and target history");
    check(windows.admit(2, 10, halfFrame * 2) == Admission::Expired, "a source with no contacts still expires after one frame");
    std::printf("%u blast window checks passed\n", checks);
}
