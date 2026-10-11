// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include "BlastClock.hpp"
#include "BombState.hpp"
#include "BombRenderPolicy.hpp"
#include "BombHighlight.hpp"

namespace {
unsigned checks{};
void require(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
bool near(float left, float right) { return std::abs(left - right) < .00001f; }
// Grants a pouch bomb for the form and adopts the world bomb the game creates from it.
remote_bomb::Ticket makeLive(remote_bomb::State &state, remote_bomb::Shape shape) {
    using namespace remote_bomb;
    state = transition(state, {shape == Shape::Sphere ? Event::SelectSphere : Event::SelectCube}).next;
    const auto grant = transition(state, {Event::UseSelected});
    state = transition(grant.next, {Event::Provisioned, shape, grant.command.ticket}).next;
    state = transition(state, {Event::InventoryActorCreated, shape}).next;
    return state.slots[index(shape)].ticket;
}
void referenceValues() {
    using namespace remote_bomb;
    require(abilityProfile(Upgrade::Base).damage == 12, "original base damage");
    require(abilityProfile(Upgrade::Plus).damage == 24, "original upgraded damage");
    require(near(abilityProfile(Upgrade::Base).rechargeSeconds, 6.f), "original base recharge");
    require(near(abilityProfile(Upgrade::Plus).rechargeSeconds, 3.f), "original upgraded recharge");
    require(near(bodyProfile(Shape::Sphere).mass, 55.f), "original sphere mass");
    require(near(bodyProfile(Shape::Cube).mass, 55.f), "original cube mass");
    require(near(bodyProfile(Shape::Sphere).damping, .5f), "original rolling damping");
    require(near(bodyProfile(Shape::Cube).height, .4f), "original cube height");
    require(near(bodyProfile(Shape::Cube).buoyancyScale, .7f), "cube has lower original buoyancy");
    require(near(bodyProfile(Shape::Sphere).buoyancyScale, 1.f), "sphere original buoyancy");
}
void originalBlastTrace() {
    using namespace remote_bomb;
    BlastClock clock;
    const float originalRadius[] = {2.001f, 4.001f, 6.f, 6.f, 6.f};
    for (float expected : originalRadius) {
        clock = advanceBlast(clock, 1.f);
        require(clock.enabled, "original pre-decrement expiry check");
        require(near(clock.radius, expected), "original unit-frame radius trace");
    }
    clock = advanceBlast(clock, 1.f);
    require(!clock.enabled, "original expiry on following update");
    require(near(advanceBlast({}, .5f).radius, 1.001f), "half-frame radius growth");
    clock = {};
    for (unsigned i = 0; i < 100; ++i)
        clock = advanceBlast(clock, 0.f);
    require(clock.enabled && near(clock.radius, .001f), "pause preserves initial blast");
    require(near(advanceBlast(clock, std::numeric_limits<float>::quiet_NaN()).radius, .001f),
            "invalid timing cannot poison blast state");
}
void independentLifecycles() {
    using namespace remote_bomb;
    State state;
    require(transition(state, {}).command.effect == Effect::None, "empty event cannot create a bomb");
    const auto sphere = transition(state, {Event::UseSelected});
    require(sphere.command.effect == Effect::Create, "first use creates sphere");
    require(transition(sphere.next, {Event::UseSelected}).command.effect == Effect::None,
            "creation cannot duplicate while asynchronous");
    const auto sphereTicket = makeLive(state, Shape::Sphere);
    const auto cubeTicket = makeLive(state, Shape::Cube);
    require(state.slots[0].phase == Phase::Live && state.slots[1].phase == Phase::Live && sphereTicket != cubeTicket,
            "sphere does not consume cube slot");
    state = transition(state, {Event::SelectSphere}).next;
    auto detonation = transition(state, {Event::UseSelected});
    require(detonation.command.effect == Effect::Detonate &&
                detonation.command.ticket == sphereTicket, "detonate only selected live form");
    require(transition(detonation.next, {Event::UseSelected}).command.effect == Effect::None,
            "pending detonation cannot repeat");
    state = transition(detonation.next, {Event::DetonationFailed, Shape::Sphere, sphereTicket}).next;
    require(state.slots[0].phase == Phase::Live, "failed native request permits retry");
    state = transition(state, {Event::Exploded, Shape::Sphere, sphereTicket}).next;
    require(state.slots[1].phase == Phase::Live, "other form remains live during recharge");
    state = advanceRecharge(state, 2.5f);
    require(near(state.slots[0].rechargeSeconds, .5f), "fractional gameplay recharge");
    auto paused = advanceRecharge(state, 0.f);
    require(near(paused.slots[0].rechargeSeconds, .5f), "menu pause preserves recharge");
    state = advanceRecharge(state, 20.f);
    require(state.slots[0].phase == Phase::Ready, "long frame clamps recharge");
    require(state.slots[1].phase == Phase::Live, "long frame never deletes other form");
    state = transition(state, {Event::Exploded, Shape::Sphere, sphereTicket}).next;
    require(state.slots[0].phase == Phase::Ready, "duplicate completion cannot reset recharge");
}
void sceneAndFailureTransactions() {
    using namespace remote_bomb;
    State state;
    const auto first = transition(state, {Event::UseSelected});
    const auto ticket = first.command.ticket;
    state = transition(first.next, {Event::SceneChanged, Shape::Sphere, {}, 9}).next;
    state = transition(state, {Event::Provisioned, Shape::Sphere, ticket}).next;
    require(state.slots[0].phase == Phase::Ready, "a previous-scene grant never enters the new scene");
    const auto second = transition(state, {Event::UseSelected});
    require(second.command.ticket != ticket, "new scene has distinct transaction identity");
    state = transition(second.next, {Event::CreationFailed, Shape::Sphere, second.command.ticket}).next;
    require(state.slots[0].phase == Phase::Ready, "failed creation spends no cooldown");
    state.upgrade = Upgrade::Base;
    const auto live = makeLive(state, Shape::Sphere);
    state = transition(state, {Event::Exploded, Shape::Sphere, live}).next;
    require(near(state.slots[0].rechargeSeconds, 6.f), "base profile recharges independently");
}
void abandonedTransactions() {
    using namespace remote_bomb;
    const auto creation = transition(State{}, {Event::UseSelected});
    const auto ticket = creation.command.ticket;
    const auto failed = transition(creation.next, {Event::CreationFailed, Shape::Sphere, ticket}).next;
    require(transition(failed, {Event::Provisioned, Shape::Sphere, ticket}).next.slots[0].phase == Phase::Ready,
            "a grant completing after creation failed is ignored");
    State live;
    const auto liveTicket = makeLive(live, Shape::Sphere);
    auto lost = transition(live, {Event::ActorLost, Shape::Sphere, liveTicket}).next;
    require(lost.slots[0].phase == Phase::Ready, "disappearing un-exploded actor releases slot");
    require(transition(lost, {Event::Exploded, Shape::Sphere, liveTicket}).next.slots[0].phase == Phase::Ready,
            "late death report cannot spend a new slot's cooldown");
    const auto exploded = transition(live, {Event::Exploded, Shape::Sphere, liveTicket}).next;
    require(transition(exploded, {Event::ActorLost, Shape::Sphere, liveTicket}).next.slots[0].phase == Phase::Recharging,
            "native explosion cleanup preserves recharge");
    const auto replacement = makeLive(lost, Shape::Sphere);
    require(replacement != liveTicket &&
                transition(lost, {Event::ActorLost, Shape::Sphere, liveTicket}).next.slots[0].phase == Phase::Live,
            "late lost-actor report cannot erase the replacement");
}
void nativeProjection() {
    using namespace remote_bomb::render_policy;
    Camera camera{{0,0,-1,5, 0,1,0,-4, 1,0,0,-3},
                  {2,0,.15f,0, 0,3,-.11f,0, 0,0,1,0, 0,0,0,1}, .1f, 2000.f};
    require(valid(camera), "finite rotated camera and asymmetric projection are accepted");
    const auto result = uniforms(camera, {7,8,9}, {0,0,1, 0,1,0, -1,0,0}, 1920, 1080);
    require(near(result.view[0][3], 3) && near(result.view[1][3], 4) && near(result.view[2][3], 5),
            "camera eye is recovered from its rotated world-to-view transform");
    require(near(result.view[0][2], 1) && near(result.view[2][0], -1),
            "view basis uses the shader's column contract");
    require(near(result.actor[0][2], 1) && near(result.actor[2][0], -1) && near(result.actor[1][3], 8),
            "native actor orientation and translation survive rendering conversion");
    require(near(result.projection[2], .15f) && near(result.projection[3], -.11f),
            "camera off-axis terms survive conversion");
    const std::array<float, 3> bombs[]{{-7, 0, 0}, {-11, 5, 2}};
    const auto span = depthRange(camera, bombs, 2);
    require(near(span[0], 10 - 1.14f) && near(span[1], 14 + 1.14f),
            "the private depth spans the live bombs' view distances with a margin");
    require(depthRange(camera, bombs, 0) == std::array<float, 2>{.1f, 2000.f},
            "with no bombs the private depth spans the camera range");
    const std::array<float, 3> close[]{{2.95f, 0, 0}};
    const auto clamped = depthRange(camera, close, 1);
    require(near(clamped[0], .1f) && clamped[1] > clamped[0], "the span never starts before the near plane");
    camera.view[0] = std::numeric_limits<float>::quiet_NaN();
    require(!valid(camera), "invalid camera cannot emit GPU geometry");
    const std::array<float, 9> turned{0,0,1, 0,1,0, -1,0,0};
    require(orientable(turned) && drawRotation(turned) == turned, "a set orientation is drawn as the game holds it");
    require(!orientable({}) && drawRotation({}) == std::array<float, 9>{1,0,0, 0,1,0, 0,0,1},
            "an unset orientation draws the body upright rather than collapsed");
    require(!orientable({.1f,0,0, 0,.1f,0, 0,0,.1f}), "a near-zero orientation is unusable");
    auto broken = turned;
    broken[4] = std::numeric_limits<float>::infinity();
    require(!orientable(broken), "a non-finite orientation is unusable");
}
void ultrahandHighlight() {
    using namespace remote_bomb::highlight;
    require(decide({false, false, 0x2, 3.f}) == Level::None, "no highlight while Ultrahand is put away");
    require(decide({true, false, 0x2, 12.f}) == Level::InReach, "a bomb in reach shows orange while Ultrahand is out");
    require(decide({true, false, 0x2, 25.f}) == Level::None, "a bomb out of reach shows nothing");
    require(decide({true, false, 0x2, std::numeric_limits<float>::quiet_NaN()}) == Level::None,
            "an unknown distance shows nothing");
    require(decide({true, true, 0x2, 25.f}) == Level::Focused, "the aimed-at bomb shows green even past the assumed reach");
    require(decide({false, true, 0x2, 3.f}) == Level::None, "a stale aim after Ultrahand is put away shows nothing");
    for (const std::uint64_t flags : {0x8002ull, 0x4002ull, 0xc002ull, 0x34002ull, 0x30002ull})
        require(decide({false, false, flags, 40.f}) == Level::Focused, "every observed Ultrahand hold state shows green");
    for (const std::uint64_t flags : {0x20002ull, 0x2ull, 0x782ull, 0x10000002ull})
        require(decide({false, false, flags, 3.f}) == Level::None,
                "release, rest, lifting and the Materials hold are not Ultrahand holds");
    const auto orange = colour(Level::InReach), green = colour(Level::Focused);
    require(orange[0] > .9f && orange[1] > .3f && orange[1] < .45f && orange[2] < .01f, "orange matches vanilla's outline");
    require(green[1] > .8f && green[0] < .1f && green[2] > .15f && green[2] < .25f, "green matches vanilla's outline");
    using remote_bomb::render_policy::centre;
    const float rows[4 * 3][4]{{-1, 0, 2, 0}, {}, {}, {3, 4, -2, 0}, {}, {}, {9, 9, 9, 0}, {}, {}, {1, 2, 0, 0}, {}, {}};
    const std::uint16_t indices[]{0, 1, 3};
    const auto middle = centre(rows, indices, 3);
    require(near(middle[0], 1) && near(middle[1], 2) && near(middle[2], 0),
            "the outline centre spans only vertices the mesh draws");
}
}
void inventoryLifecycle() {
    using namespace remote_bomb;
    State state;
    state.scene = 17;
    auto grant = transition(state, {Event::UseSelected});
    const auto grantTicket = grant.command.ticket;
    state = transition(grant.next, {Event::Provisioned, Shape::Sphere, grantTicket}).next;
    require(state.slots[0].phase == Phase::Stored, "native grant reserves a pouch item without a world actor");
    require(transition(state, {Event::UseSelected}).command.effect == Effect::None,
            "quick L cannot duplicate a stored bomb");
    require(transition(state, {Event::Provisioned, Shape::Sphere, {16,grantTicket.serial}})
                .next.slots[0].ticket == grantTicket, "previous-scene inventory completion is ignored");
    state = transition(state, {Event::InventoryActorCreated, Shape::Sphere}).next;
    const auto live = state.slots[0].ticket;
    require(state.slots[0].phase == Phase::Live && live != grantTicket,
            "native throw or arrow spawn gets a distinct world lifetime");
    require(transition(state, {Event::InventoryActorCreated, Shape::Sphere}).next.slots[0].ticket == live,
            "additional native instances cannot replace an owned form");
    auto detonate = transition(state, {Event::UseSelected});
    require(detonate.command.effect == Effect::Detonate && detonate.command.ticket == live,
            "quick L remotely detonates the adopted inventory actor");
    state = transition(detonate.next, {Event::Exploded, Shape::Sphere, live}).next;
    require(state.slots[0].phase == Phase::Recharging,
            "inventory-origin bombs retain the original recharge");
    require(transition(state, {Event::InventoryActorCreated, Shape::Sphere}).next.slots[0].phase == Phase::Recharging,
            "late native spawn cannot bypass recharge");
    state = advanceRecharge(state, 3.f);
    require(state.slots[0].phase == Phase::Ready, "recharge permits native replenishment");
    state = transition(state, {Event::InventoryActorCreated, Shape::Cube}).next;
    require(state.slots[1].phase == Phase::Live && state.slots[0].phase == Phase::Ready,
            "saved inventory cube can be adopted independently after a scene reset");
    auto regrant = transition(state, {Event::UseSelected});
    const auto stored = regrant.command.ticket;
    state = transition(regrant.next, {Event::Provisioned, Shape::Sphere, stored}).next;
    require(transition(state, {Event::StockLost, Shape::Sphere, {state.scene, stored.serial + 1}})
                .next.slots[0].phase == Phase::Stored, "stale stock loss is ignored");
    state = transition(state, {Event::StockLost, Shape::Sphere, stored}).next;
    require(state.slots[0].phase == Phase::Recharging && state.slots[0].rechargeSeconds == 3.f &&
            state.slots[1].phase == Phase::Live, "lost stock recharges only its own form");
    require(transition(advanceRecharge(state, 1.f), {Event::StockLost, Shape::Sphere, stored})
                .next.slots[0].rechargeSeconds == 2.f, "repeated stock loss cannot restart recharge");
    require(transition(state, {Event::StockLost, Shape::Cube, state.slots[1].ticket}).next.slots[1].phase == Phase::Live,
            "stock loss never ends a live bomb");
    state = advanceRecharge(state, 3.f);
    require(state.slots[0].phase == Phase::Ready, "lost stock is replenished after recharge");
    regrant = transition(state, {Event::UseSelected});
    const auto restored = regrant.command.ticket;
    state = transition(regrant.next, {Event::Provisioned, Shape::Sphere, restored}).next;
    require(transition(state, {Event::StockReplaced, Shape::Sphere, {state.scene, restored.serial + 1}})
                .next.slots[0].phase == Phase::Stored, "stale pouch replacement is ignored");
    const auto replaced = transition(state, {Event::StockReplaced, Shape::Sphere, restored}).next;
    require(replaced.slots[0].phase == Phase::Ready && replaced.slots[0].rechargeSeconds == 0.f,
            "a replaced pouch regrants without recharge");
    require(transition(state, {Event::StockReplaced, Shape::Cube, state.slots[1].ticket}).next.slots[1].phase == Phase::Live,
            "pouch replacement never ends a live bomb");
}
void detonationAfterSwitching() {
    using namespace remote_bomb;
    State state;
    state.scene = 17;
    state = transition(state, {Event::InventoryActorCreated, Shape::Sphere}).next;
    state = transition(state, {Event::SelectCube}).next;
    state.slots[1].phase = Phase::Stored;
    require(detonationTarget(state) == Shape::Sphere,
            "quick L reaches the sphere left out after switching to a stored cube");
    state.slots[1].phase = Phase::Recharging;
    require(detonationTarget(state) == Shape::Sphere, "a recharging cube does not hide the live sphere");
    state = transition(state, {Event::SelectSphere}).next;
    const auto detonate = transition(state, {Event::UseSelected});
    require(detonate.command.effect == Effect::Detonate && detonate.command.shape == Shape::Sphere,
            "selecting the target lets quick L detonate it");
    state.selected = Shape::Cube;
    state.slots[1].phase = Phase::Live;
    require(detonationTarget(state) == Shape::Cube, "a live selected form keeps quick L");
    state.slots[1].phase = Phase::DetonationRequested;
    require(detonationTarget(state) == Shape::Cube, "a pending detonation keeps its own form");
    state.slots = {};
    require(detonationTarget(state) == Shape::Cube, "with nothing out quick L stays on the selected form");
}
int main() {
    referenceValues();
    originalBlastTrace();
    independentLifecycles();
    sceneAndFailureTransactions();
    abandonedTransactions();
    nativeProjection();
    ultrahandHighlight();
    inventoryLifecycle();
    detonationAfterSwitching();
    std::cout << checks << " Remote Bomb checks passed\n";
}
