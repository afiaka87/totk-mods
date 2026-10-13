// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/pure/CryonisCore.hpp"
#include "../src/pure/CryonisGrowth.hpp"
#include "../src/pure/CryonisLift.hpp"
#include "../src/pure/CryonisWater.hpp"
#include "../src/pure/CryonisInput.hpp"
#include "CryonisWheel.hpp"
#include "CryonisEquipment.hpp"
#include "CryonisNode.hpp"
#include "../src/pure/CryonisSelection.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace cryonis;
void check(bool value, const char *label) {
    if (!value) {
        std::fprintf(stderr, "FAIL %s\n", label);
        std::exit(1);
    }
}
int main() {
    for (unsigned broken = 0; broken <= 3; ++broken) {
        const std::uintptr_t pointers[]{0x100000, 0x200000, 0x300000, 0x400000};
        const std::uintptr_t offsets[]{8, 8, 0x1F8};
        unsigned inspected{};
        const auto readNode = [&](std::uintptr_t address) {
            check(inspected < 3 && address == pointers[inspected] + offsets[inspected],
                  "execute owner lookup never reads through a rejected link");
            const auto index = inspected++;
            return index == broken ? std::uintptr_t{0x1000000} : pointers[index + 1];
        };
        const auto validNode = [](std::uintptr_t p) {
            return p >= 0x100000 && p <= 0x500000 - sizeof(std::uintptr_t) && !(p & 7);
        };
        check(engine::executeActor(pointers[0], readNode, validNode) ==
                  (broken == 3 ? pointers[3] : 0),
              "native execute owner chain rejects the observed unmapped pointer");
        check(inspected == (broken == 3 ? 3 : broken + 1),
              "execute owner lookup stops at the first invalid pointer");
    }
    const std::uintptr_t chain[]{0x100000, 0x200000, 0x300000, 0x400000, 0x500000};
    const std::size_t offsets[]{layout::actor::kComponents, layout::actor::kPhysics,
                              layout::actor::kControllerSet, 0x150};
    for (unsigned broken = 0; broken <= 4; ++broken) {
        unsigned inspected{};
        const auto validChain = [](std::uintptr_t p) {
            return p >= 0x100000 && p <= 0x600000 - sizeof(std::uintptr_t) && !(p & 7);
        };
        const auto readChain = [&](std::uintptr_t p) {
            check(inspected < 4 && p == chain[inspected] + offsets[inspected],
                  "physics lookup never reads through a rejected chain link");
            const auto index = inspected++;
            return index == broken ? std::uintptr_t{0x10000} : chain[index + 1];
        };
        check(engine::physicalBody(chain[0], readChain, validChain) ==
                  (broken == 4 ? chain[4] : 0),
              "every low physics-chain pointer is rejected before dereference");
        check(inspected == (broken == 4 ? 4 : broken + 1),
              "physics lookup stops at the first invalid link");
    }
    for (const auto name : {"Animal_Fish_E", "Player", "Obj_Cryonis", "", "\xD8"}) {
        const auto address = reinterpret_cast<std::uintptr_t>(name);
        const auto length = std::strlen(name);
        check(engine::actorName(address,
                  [](std::uintptr_t p) { return *reinterpret_cast<const unsigned char *>(p); },
                  [&](std::uintptr_t p) { return p >= address && p <= address + length; }) ==
                  (length != 0 && static_cast<unsigned char>(name[0]) != 0xD8),
              "binary process identity is not treated as an actor name");
    }
    check(!engine::actorName(0x10000,
              [](std::uintptr_t) { check(false, "unmapped identity must not be read"); return 0; },
              [](std::uintptr_t) { return false; }),
          "unmapped identity is rejected before reading its first byte");
    const std::uintptr_t actor = 0x100000, components = 0x200000, equipment = 0x300000;
    unsigned reads{};
    std::uintptr_t registry = components, slot = equipment;
    const auto validAddress = [&](std::uintptr_t address) {
        return address >= actor && address <= 0x400000 - sizeof(std::uintptr_t) &&
               (address & 7) == 0;
    };
    const auto readSlot = [&](std::uintptr_t address) {
        ++reads;
        if (address == actor + layout::actor::kComponents)
            return registry;
        check(address == components + layout::actor::kDynamicEquipment,
              "equipment reader never touches invalid registry memory");
        return slot;
    };
    check(engine::equipmentComponent(actor, readSlot, validAddress) == equipment && reads == 2,
          "native equipment slot resolves from a valid component registry");
    registry = 0;
    reads = 0;
    check(engine::equipmentComponent(actor, readSlot, validAddress) == 0 && reads == 1,
          "missing registry returns without dereferencing its equipment slot");
    registry = 0x10000;
    reads = 0;
    check(engine::equipmentComponent(actor, readSlot, validAddress) == 0 && reads == 1,
          "invalid registry is rejected before reading native memory");
    registry = components;
    slot = 0;
    check(engine::equipmentComponent(actor, readSlot, validAddress) == 0,
          "actor without equipment has no owner lookup");
    reads = 0;
    check(engine::equipmentComponent(0, readSlot, validAddress) == 0 && reads == 0,
          "null actor does not trigger native reads");
    check(kOriginalWidth == 3 && kOriginalHeight == 4 && kSizePercent == 90,
          "percentage tuning preserves recovered dimensions");
    check(std::fabs(kWidth - 2.7f) < .00001f && std::fabs(kHeight - 3.6f) < .00001f &&
              std::fabs(kSettledSubmerge - .18f) < .00001f,
          "90 percent dimensions with five percent submerged");
    const float quarter = 1.57079632679f;
    for (Vec3 link :
         {Vec3{0, 0, 4}, Vec3{4, 0, 0}, Vec3{0, 0, -4}, Vec3{-4, 0, 0}, Vec3{3, 0, 3}}) {
        const auto yaw = faceLink({}, link);
        const auto face = rotateYaw({0, 0, 1}, yaw);
        check(std::fabs(face.x - link.x / length(link)) < .00001f &&
                  std::fabs(face.z - link.z / length(link)) < .00001f,
              "preview face follows Link around the full circle");
        const auto point = rotateYaw(rotateYaw({1.1f, 2, -.7f}, yaw), -yaw);
        check(length(add(point, {-1.1f, -2, .7f})) < .00001f,
              "render rotation and inverse hit transform agree");
    }
    check(faceLink({}, {}, quarter) == quarter, "coincident target retains stable facing");
    State facing;
    facing.aiming = true;
    Target orient{{0, 2, 0}, {0, 1, 0}, true, -1, .7f};
    check(facing.commit(facing.reserve(orient), orient),
          "oriented preview constructs successfully");
    orient.yaw = faceLink(orient.base, {-4, 2, 0});
    check(facing.pillars[0].yaw == .7f,
          "constructed tower pins confirmation angle when Link circles it");
    const auto corner = footprintPoint({}, 1.35f, 0, 1.35f, .7853981634f);
    check(std::fabs(corner.x - 1.9091883f) < .00001f && std::fabs(corner.z) < .00001f,
          "rotated footprint matches independent geometry");
    check(lift::footprint({}, {{1.35f, 0, -.05f}, {1.45f, 1, .05f}}, .7853981634f),
          "diagonal vertex can catch riders");
    check(!lift::footprint({}, {{1.3f, 0, 1.3f}, {1.4f, 1, 1.4f}}, .7853981634f),
          "world bounding-box corner is outside rotated footprint");
    State s;
    Target t{{0, 2, 0}, {0, 1, 0}, true, -1};
    check(!s.reserve(t).valid, "idle cannot place");
    s.aiming = true;
    for (unsigned i = 0; i < 3; ++i) {
        s.aiming = true;
        auto r = s.reserve(t);
        check(r.valid && r.remove == 0, "quick reactivation reserves successive pillars");
        check(s.commit(r, t), "ready native spawn commits");
        check(!s.aiming && s.cooldown == 50,
              "construction closes targeting and starts original cooldown");
        s.aiming = true;
        check(!s.reserve(t).valid, "quick reactivation cannot bypass cooldown");
        const float before = s.cooldown;
        s.tick(50, false);
        check(s.cooldown == before, "menus pause cooldown");
        s.tick(50, true);
    }
    s.aiming = true;
    auto r = s.reserve(t);
    check(r.remove == 1, "fourth replaces oldest");
    check(s.pillars[0].id == 1, "reservation alone keeps old ice");
    check(s.commit(r, t) && s.pillars[0].id == 4, "successful fourth commits replacement");
    check(!s.commit(r, t), "stale reservation rejected");
    s.tick(50, true);
    s.aiming = true;
    t.pillar = 1;
    check(!s.reserve(t).valid, "selected existing pillar cannot also place");
    t.pillar = -1;
    s.remove(2);
    check(s.reserve(t).slot == 1, "manual destruction frees slot");
    r = s.reserve(t);
    s.aiming = false;
    check(s.commit(r, t) && !s.aiming, "B during pending creation is not undone by completion");
    s.reset();
    check(!s.aiming && s.nextId == 1 && !s.pillars[0].occupied, "scene reset drops ownership");
    Animation a;
    a.advance(3);
    check(a.frame == 6, "first six animation frames at rate two");
    a.advance(1);
    check(a.frame == 10, "next four at rate four");
    a.advance(16);
    check(a.frame == 90, "middle eighty at rate five");
    a.advance(10);
    check(a.finished(), "original hundred-frame animation finishes after thirty gameplay ticks");
    Animation b;
    for (unsigned i = 0; i < 120; ++i)
        b.advance(0.25f);
    check(b.frame == 100, "fractional and batched time agree");
    check(originalPose(0).width < .02f && originalPose(0).height < .02f,
          "original ice begins collapsed");
    check(originalPose(12).width > .99f && originalPose(12).height < .1f,
          "width expands before height");
    check(std::fabs(originalPose(100).height - (.505f + 73 * .006755905f)) < .00001f,
          "original final quantized scale retained");
    check(originalPose(50).height > .4f && originalPose(50).height < .5f,
          "independent midpoint bound rejects linear frame growth");
    float last = 0;
    for (unsigned i = 10; i <= 100; ++i) {
        const auto top = collisionHeight(float(i));
        check(top >= last - .0001f, "collision rises after original initial dip");
        last = top;
    }
    for (unsigned i = 0; i <= 100; ++i) {
        const auto p = collisionBase({219.4935f, 115.004196f, 1541.208374f}, float(i));
        check(p.x == 219.4935f && p.z == 1541.208374f, "growth never displaces collider sideways");
        check(std::fabs(p.y + 3.6f - 115.004196f - emergenceHeight(float(i))) < .00002f,
              "unit-size top follows emergence growth");
        check(p.y <= 115.004196f, "full-size body begins submerged");
    }
    check(emergenceHeight(0) < -.6096f && std::fabs(emergenceHeight(100) - 3.42f) < .00001f,
          "body starts beneath capture band and ends at the submerged 90-percent height");
    check(std::fabs(visualBase({0, 2, 0}, 100).y - 1.82f) < .00001f,
          "finished visual stays eighteen centimetres underwater");
    const Vec3 surface{0, 2, 0};
    const lift::Bounds shallowFish{{-.2f, 1.4f, -.2f}, {.2f, 1.6f, .2f}};
    check(lift::capture(surface, shallowFish), "fish within two feet below surface is captured");
    check(!lift::capture(surface, {{-.2f, .5f, -.2f}, {.2f, 1.38f, .2f}}),
          "fish wholly below the two-foot band is excluded");
    check(lift::capture(surface, {{-.4f, .8f, -.4f}, {.4f, 2.1f, .4f}}),
          "swimmer with feet deeper than capture band is caught by upper bounds");
    check(!lift::capture(surface, {{2, 1.4f, -.2f}, {2.4f, 1.6f, .2f}}),
          "actor beside footprint is not captured");
    check(!lift::capture(surface, {{-.2f, 2.1f, -.2f}, {.2f, 2.3f, .2f}}),
          "floating actor wholly above water is not pulled down");
    auto fish = shallowFish;
    for (unsigned frame = 0; frame <= 100; ++frame) {
        const float delta = lift::rise(surface, float(frame), fish);
        check(delta >= 0, "rider movement only raises actors");
        fish.min.y += delta;
        fish.max.y += delta;
        check(fish.min.y >= surface.y + emergenceHeight(float(frame)) + kRiderClearance - .0001f,
              "fish bottom stays above growing top");
    }
    check(std::fabs(fish.min.y - 5.46f) < .0001f, "fish ends just above completed platform");
    check(lift::rise(surface, 100, {{2, 2, 0}, {3, 3, 1}}) == 0,
          "walking beyond footprint stops lifting");
    check(lift::rise(surface, 100, {{0, 7, 0}, {1, 8, 1}}) == 0,
          "jumping above platform is never pulled down");
    check(!lift::valid({{0, 0, 0}, {0, 1, 1}}),
          "empty collision bounds cannot become rider geometry");
    {
        const float reach = water::kHighlightReach;
        check(water::coordinate(0, reach) == -375.0f && water::coordinate(63, reach) == 375.0f,
              "water grid stays bounded");
        check(reach > water::kHighlightFadeStart && water::kHighlightFadeStart == 300.0f &&
                  water::kHighlightFadeStart > kReach,
              "river highlighting extends well beyond placement range before fading");
        for (unsigned i = 1; i < 64; ++i)
            check(water::coordinate(i, reach) > water::coordinate(i - 1, reach),
                  "expanded water grid never folds");
        for (unsigned i = 17; i < 48; ++i)
            check(water::coordinate(i, reach) - water::coordinate(i - 1, reach) == 2,
                  "near shore retains two-metre query density");
    }
    const auto flat = [](float, float) { return Water{2, -5, true}; };
    auto hit = waterTarget(Ray{{0, 10, 0}, {0, -1, 0}, true}, flat);
    check(hit.valid && std::fabs(hit.base.y - 2) < 0.001f, "ray lands on queried water");
    check(!waterTarget(Ray{{0, 10, 0}, {0, 1, 0}, true}, flat).valid, "upward aiming rejected");
    check(!waterTarget(Ray{{0, 80, 0}, {0, -1, 0}, true}, flat).valid,
          "original sixty-metre reach enforced");
    check(
        !waterTarget(Ray{{0, 10, 0}, {0, -1, 0}, true}, [](float, float) { return Water{}; }).valid,
        "empty space rejected");
    check(!waterTarget(Ray{{0, 10, 0}, {0, -1, 0}, true},
                       [](float, float) { return Water{2, 5, true}; })
               .valid,
          "bad native depth rejected");
    const Target verified{{0, 2, 0}, {0, 1, 0}, true, -1};
    const Ray oldRay{{0, 10, 0}, {0, -1, 0}, true};
    auto newRay = oldRay;
    newRay.eye.x = .2f;
    auto moved = verified;
    moved.base.x = .2f;
    check(!target::sameRay(oldRay, newRay), "walking exceeds the old preview deletion tolerance");
    check(target::compatibleTarget(verified, moved, oldRay, newRay),
          "small motion retains the exact validated placement");
    moved.base.x = 2;
    check(!target::compatibleTarget(verified, moved, oldRay, newRay),
          "different water target cannot reuse placement clearance");
    moved = verified;
    moved.base.y += .1f;
    check(!target::compatibleTarget(verified, moved, oldRay, newRay),
          "different surface height requires a new query");
    moved = verified;
    moved.valid = false;
    check(!target::compatibleTarget(verified, moved, oldRay, newRay),
          "dry ground cannot retain a validated water target");
    moved = verified;
    moved.pillar = 0;
    check(!target::compatibleTarget(verified, moved, oldRay, newRay),
          "placement cannot turn into unvalidated shatter");
    Pillar existing{{0, 2, 0}, 1, {100}, true};
    moved = verified;
    moved.yaw = .1f;
    check(!target::compatibleTarget(verified, moved, oldRay, oldRay),
          "rotation requires fresh clearance");
    check(target::hitOnPillar({0, 4, 1.35f}, existing),
          "native hit on the front face selects settled ice");
    check(target::hitOnPillar({0, 5.42f, 0}, existing),
          "native hit on top selects ice while standing above it");
    check(!target::hitOnPillar({0, 4, 3}, existing),
          "nearer native occluder cannot select ice behind it");
    check(!target::hitOnPillar({2, 4, 1.5f}, existing), "hit beside ice cannot select it");
    check(!target::hitOnPillar({0, 8, 0}, existing), "hit above ice cannot select it");
    existing.growth.frame = 0;
    existing.yaw = .7853981634f;
    existing.growth.frame = 100;
    check(target::hitOnPillar({1.55f, 4, 0}, existing) &&
              !target::hitOnPillar({1.3f, 4, 1.3f}, existing),
          "shatter checks rotated physical footprint");
    existing.yaw = 0;
    existing.growth.frame = 0;
    check(!target::hitOnPillar({0, 4, 1.35f}, existing),
          "future height of growing ice cannot select it");
    existing.occupied = false;
    check(!target::hitOnPillar({0, 2, 0}, existing), "removed slot cannot be selected");
    Target picked{{0, 2, 0}, {0, 1, 0}, true, 1};
    check(target::selected(1, picked, true) && !target::selected(0, picked, true) &&
              !target::selected(2, picked, true),
          "only the selected slot is highlighted");
    check(!target::selected(1, picked, false), "B removes hover highlight");
    check(actionReady(picked, 50, false), "shatter remains available during placement cooldown");
    check(!actionReady(picked, 0, true), "pending creation excludes a competing shatter");
    picked.valid = false;
    check(!target::selected(1, picked, true) && !actionReady(picked, 0, false),
          "stale selection neither highlights nor enables A");
    check(!actionReady(verified, 1, false) && actionReady(verified, 0, false),
          "water placement waits for recharge");
    input::AimButtons ownership;
    const auto native = [&](bool aiming, std::uint64_t buttons, bool fresh = true) {
        return buttons & ~ownership.mask(aiming, buttons, fresh);
    };
    check(native(true, input::A) == 0, "construction A cannot trigger native Climb down");
    check(native(true, input::A) == 0, "A held during pending construction stays consumed");
    check(native(true, input::A) == 0, "A held after completion stays consumed");
    check(native(true, input::A | input::B) == 0, "B exit consumes both buttons when A is held");
    check(native(false, input::A | input::B) == 0,
          "exiting aim cannot create a native A or sprint edge");
    check(native(false, 0, false) == 0 && ownership.held == (input::A | input::B),
          "no new controller sample cannot release consumed holds");
    check(native(false, input::A) == 0, "releasing only B cannot leak the remaining A hold");
    check(native(false, 0) == 0 && native(false, input::A) == input::A,
          "a new A press after physical release returns to native gameplay");
    check(native(true, input::X | input::L) == (input::X | input::L),
          "physical L reaches the held native wheel while targeting; X stays native");
    check(native(false, input::X | input::L) == (input::X | input::L),
          "targeting exit cannot trap the native wheel L hold");
    native(false, 0);
    alignas(8) unsigned char screen[0x2A0]{};
    const auto address = reinterpret_cast<std::uintptr_t>(screen);
    auto put = [&]<class T>(unsigned at, T value) {
        std::memcpy(screen + at, &value, sizeof(value));
    };
    auto read = [&]<class T>(std::uintptr_t at) {
        T value{};
        std::memcpy(&value, reinterpret_cast<const void *>(at), sizeof(value));
        return value;
    };
    put(0x1A8, std::int32_t(8));
    put(0x23C, std::int32_t(3));
    put(0x245, std::uint8_t(1));
    put(0x27C, std::uint32_t(7));
    check(!engine::amiiboSelected(address, read), "Camera hover leaves the Stasis chord untouched");
    put(0x27C, std::uint32_t(8));
    check(engine::amiiboSelected(address, read), "native Amiibo ID activates Cryonis hover");
    input::Activation activation;
    const auto requested =
        activation.step({1, true, true}, input::L | input::StickR, input::StickR, true, true, true);
    check(requested.event == input::Event::Requested && (requested.add & input::B),
          "Amiibo chord cancels the native wheel before aiming");
    const auto activated =
        activation.step({1, false, false, true, true, true}, 0, 0, true, true, true);
    check(activated.beginAim && activated.event == input::Event::Activated,
          "aim begins only after a cancelled wheel closes");
    input::Activation cancelled;
    cancelled.step({2, true, true}, input::L | input::StickR, input::StickR, true, true, true);
    check(!cancelled.step({2, false, false, true, false, true}, 0, 0, true, true, true).beginAim,
          "native selection without cancellation cannot activate Cryonis");
    input::RecentAbility recent;
    input::Wheel idle{3, false, false, true, true, true};
    check(!recent.step(idle, input::L, input::L, true, true, true, false) && !recent.tap,
          "L is native before Cryonis activation");
    recent.remember();
    check(!recent.step(idle, input::L, input::L, true, true, true, false) && recent.tap,
          "remembered Cryonis arms a quick L tap");
    check(input::recentGameplayKeys(input::kMappedL | 5, recent.tap) == 5,
          "only gameplay ability key is suppressed; unrelated keys survive");
    check(!recent.step(idle, input::L, 0, false, true, true, false) && recent.tap,
          "missing fresh sample does not invent an L release");
    check(recent.step(idle, 0, 0, true, true, true, false) && !recent.tap,
          "physical quick release re-enters Cryonis");
    recent.step(idle, input::L, input::L, true, true, true, false);
    auto wheelOpen = input::Wheel{4, true, false};
    check(!recent.step(wheelOpen, input::L, 0, true, true, true, false) && !recent.tap,
          "native wheel opening cancels quick tap without delaying the wheel");
    check(!recent.step({4, false, false, true, false, true}, 0, 0, true, true, true, false) &&
              !recent.remembered,
          "using another native ability replaces Cryonis memory");
    check(input::recentGameplayKeys(input::kMappedL | 5, recent.tap) == (input::kMappedL | 5),
          "other abilities regain normal quick L");
    recent.remember();
    idle = {5, false, false, true, true, true};
    recent.nativeAccepted();
    check(!recent.remembered && !recent.tap &&
              !recent.step(idle, input::L, input::L, true, true, true, false),
          "accepted Amiibo replaces memory despite cached cancellation");
    check(!recent.step(idle, 0, 0, true, true, true, false),
          "native Amiibo release cannot reopen Cryonis");
    check(!input::cancelNativeAmiibo(recent.remembered, true, true, 0),
          "native Amiibo accepted selection stays available");
    recent.remember();
    check(input::cancelNativeAmiibo(recent.remembered, true, true, 0),
          "re-entering Cryonis finishes lingering native Amiibo aiming");
    check(!input::cancelNativeAmiibo(true, false, true, 0) &&
              !input::cancelNativeAmiibo(true, true, false, 0),
          "pause and another player cannot cancel native aiming");
    for (unsigned state : {1u, 2u, 3u})
        check(!input::cancelNativeAmiibo(true, true, true, state),
              "scan and drop processing remain native");
    for (auto flags : {0u, 0x12345u, 0xc1234567u, 0xffffffffu}) {
        const auto cancelledFlags = input::nativeCancelFlags(flags);
        check((cancelledFlags & 0xc0000000u) == 0x80000000u &&
                  (cancelledFlags & 0x3fffffffu) == (flags & 0x3fffffffu),
              "native cancellation preserves low node flags and clears failure bit");
    }
    recent.nativeAccepted();
    check(!input::cancelNativeAmiibo(recent.remembered, true, true, 0),
          "deliberate native Amiibo replacement clears custom lifecycle suppression");
    recent.remember();
    recent.step(idle, input::L, input::L, true, true, true, false);
    check(!recent.step(idle, 0, 0, true, false, true, false) && !recent.tap,
          "pause cancels an unfinished tap");
    recent.step(idle, input::L, input::L, true, true, true, false);
    recent.cancelTap();
    check(recent.remembered && !recent.tap,
          "scene changes discard input holds but retain the most recent ability");
    check(!recent.step(idle, input::L, input::L, true, true, true, true) && !recent.tap,
          "initiating chord cannot become a second quick activation");
    recent.step(idle, input::L, input::L, true, true, true, false);
    check(!recent.step(idle, input::L | input::B, input::B, true, true, true, false) && !recent.tap,
          "B cancels a pending quick tap");
    check(!recent.step(idle, input::L, input::L, true, true, false, false) && !recent.tap,
          "pending construction cannot reactivate targeting");
    check(!recent.step(idle, input::L, input::L, true, true, false, false) && !recent.tap &&
              recent.remembered,
          "swimming blocks quick activation without forgetting Cryonis");
    check(!recent.step(idle, 0, 0, true, true, false, false),
          "releasing L while swimming cannot enter targeting");
    recent.step(idle, input::L, input::L, true, true, true, false);
    check(!recent.step(idle, 0, 0, true, true, false, false) && !recent.tap,
          "entering swim between press and release cancels the tap");
    recent.step(idle, input::L, input::L, true, true, true, false);
    check(recent.step(idle, 0, 0, true, true, true, false),
          "returning to shore permits remembered quick activation");
    input::Activation swimmingChord;
    const auto refused = swimmingChord.step({5, true, true}, input::L | input::StickR,
                                            input::StickR, true, true, false);
    check(refused.event == input::Event::Unavailable && !refused.beginAim && !refused.add,
          "swimming rejects the chord without cancelling the native wheel");
    const input::Wheel pickedOpen{6, true, false, false, false, false, true},
        pickedExit{6, false, false, true, true, false, true},
        pickedClosed{6, false, false, true, true, true, true},
        pickedAccepted{7, false, false, true, false, true, true};
    check(input::unpack(input::pack(pickedClosed)).chosen &&
              !input::unpack(input::pack(idle)).chosen,
          "the wheel page choice survives the shared wheel word");
    input::Activation page;
    check(page.step(pickedOpen, input::L, 0, true, true, true).event == input::Event::None,
          "a page choice waits for the wheel to close");
    const auto pageRequest = page.step(pickedExit, 0, 0, true, true, true);
    check(pageRequest.event == input::Event::Requested && !pageRequest.add,
          "a page choice needs no extra cancel");
    const auto pageAim = page.step(pickedClosed, 0, 0, true, true, true);
    check(pageAim.beginAim && pageAim.event == input::Event::Activated,
          "a page choice aims Cryonis after the cancelled wheel closes");
    page.reset();
    check(page.step(pickedClosed, 0, 0, true, true, true).event == input::Event::None &&
              !page.pendingEpoch,
          "a page choice acts once, even across a scene reset");
    check(page.step(pickedAccepted, 0, 0, true, true, true).event == input::Event::None,
          "a page choice needs a cancelled exit");
    input::Activation pageRefused;
    check(pageRefused.step(pickedClosed, 0, 0, true, true, false).event ==
              input::Event::Unavailable,
          "a page choice while swimming is refused");
    std::puts("Cryonis lifecycle, pause, original playback boundaries, and water targeting passed");
}
