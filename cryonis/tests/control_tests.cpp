// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisControl.hpp"
#include "CryonisRenderPolicy.hpp"
#include <cstdlib>
#include <cstdio>
using namespace cryonis;
static void check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}
int main() {
    control::Memory memory;
    control::Context context{
        {1, true, true}, input::L | input::StickR, input::StickR, true, true, false, false, false};
    const auto request = control::transition(memory, context);
    check(!memory.activation.pendingEpoch && !memory.recent.remembered,
          "transition leaves previous memory unchanged");
    check(request.chord.event == input::Event::Requested && request.chord.add == input::B,
          "Amiibo chord requests native B cancellation");
    context = {{1, false, false, true, true, true}, 0, 0, true, true, false, false, false};
    const auto enter = control::transition(request.next, context);
    check(enter.aim == control::AimChange::Enter && enter.next.recent.remembered,
          "cancelled wheel enters and remembers Cryonis");
    check(control::action(true, true, input::A, true, false, {{}, {}, true}) ==
              control::Action::None,
          "chord completion cannot construct on same frame");
    memory = enter.next;
    context = {
        {1, false, false, true, true, true}, input::L, input::L, true, true, false, true, false};
    const auto tap = control::transition(memory, context);
    check(tap.aim == control::AimChange::None && tap.next.recent.tap,
          "press alone cannot toggle targeting");
    context.buttons = 0;
    context.pressed = 0;
    const auto exit = control::transition(tap.next, context);
    check(exit.aim == control::AimChange::Exit && exit.next.recent.remembered,
          "L release exits while preserving memory");
    context.buttons = input::L;
    context.pressed = input::L;
    context.otherHoldsL = true;
    const auto yielded = control::transition(memory, context);
    check(yielded.aim == control::AimChange::None && !yielded.next.recent.tap &&
              yielded.next.recent.remembered,
          "L held by another ability is left to it");
    check(yielded.next.yieldedPress, "a press during another ability's hold is noted");
    context.otherHoldsL = false;
    context.pressed = 0;
    const auto adopted = control::transition(yielded.next, context);
    check(adopted.next.recent.tap && !adopted.next.yieldedPress &&
              adopted.aim == control::AimChange::None,
          "the press is kept once the other ability hands L back");
    context.buttons = 0;
    const auto handedRelease = control::transition(adopted.next, context);
    check(handedRelease.aim == control::AimChange::Exit && handedRelease.next.recent.remembered,
          "releasing the handed-back press acts like a normal tap");
    context.buttons = input::L;
    context.otherHoldsL = true;
    const auto released = control::transition(
        control::transition(memory, context).next,
        {context.wheel, 0, 0, true, true, false, true, false, false});
    check(!released.next.yieldedPress && !released.next.recent.tap,
          "a press released while still held elsewhere is not kept");
    context.otherHoldsL = false;
    context.buttons = 0;
    context.pressed = 0;
    context.aiming = false;
    context.swimming = true;
    context.buttons = input::L;
    context.pressed = input::L;
    const auto swim = control::transition(exit.next, context);
    check(swim.aim == control::AimChange::None && swim.next.recent.remembered &&
              !swim.next.recent.tap,
          "swim refusal retains memory");
    context.swimming = false;
    context.wheel = {2, true, false};
    context.pressed = 0;
    const auto wheel = control::transition(tap.next, context);
    check(!wheel.quick && !wheel.next.recent.tap, "held L wheel stays native");
    context.wheel = {2, false, false, true, false, true};
    context.buttons = 0;
    const auto native = control::transition(wheel.next, context);
    check(!native.next.recent.remembered && native.aim == control::AimChange::None,
          "native selection replaces memory");
    for (bool aiming : {false, true})
        for (bool running : {false, true})
            for (bool pending : {false, true}) {
                const auto action = control::action(aiming, running, input::A | input::B, false,
                                                    pending, {{}, {}, true, 1});
                check(action ==
                          (aiming && running ? control::Action::Cancel : control::Action::None),
                      "B priority, pause and idle action gates");
            }
    check(control::action(true, true, input::A, false, true, {}) ==
              control::Action::CreationPending,
          "pending construction refuses another transaction");
    check(control::action(true, true, input::A, false, false, {{}, {}, true, 1}) ==
              control::Action::Shatter,
          "selected pillar shatters");
    check(control::action(true, true, input::A, false, false, {}) == control::Action::Construct,
          "invalid water reaches native placement refusal");
    input::AimButtons held;
    check((held.mask(true, input::A, true) & input::A) != 0, "targeting takes A before exit");
    check((held.mask(false, input::A, true) & input::A) != 0,
          "construction hold cannot become Climb down");
    check(held.mask(false, 0, true) == 0, "physical release clears A ownership");
    render_policy::Camera camera{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}, {}, .1f, 1000};
    camera.projection[0] = camera.projection[5] = 1;
    Presentation p;
    p.aiming = true;
    p.target = {{2, 4, -8}, {0, 1, 0}, true, -1, .5f};
    p.previewFrames = 30;
    const auto common = render_policy::uniforms(camera, p, 1280, 720);
    const auto preview = render_policy::pillars(common, p, camera);
    check(preview.count == 1 && preview.values[0].base[3] == 1,
          "preview is a separate translucent draw");
    check(preview.values[0].base[1] == 4 - kSettledSubmerge && preview.values[0].transform[3] == 4,
          "surface marker and submerged preview stay distinct");
    p.aiming = false;
    for (unsigned i = 0; i < kPillarLimit; ++i) {
        p.pillars[i].occupied = true;
        p.pillars[i].base = {0, 0, -float(i + 1) * 5};
        p.pillars[i].growth.frame = 47;
    }
    const auto batch = render_policy::pillars(common, p, camera);
    check(batch.count == 3 && batch.values[0].base[2] == -15 && batch.values[2].base[2] == -5,
          "solid pillars preserve far-to-near depth order");
    std::puts("Pure control transitions, action priority, ownership and render transforms passed");
}
