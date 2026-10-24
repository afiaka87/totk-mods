// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombRuntime.hpp"
#include "BombPhysics.hpp"
#include "BombBlast.hpp"
#include "BombInput.hpp"
#include "BombInventory.hpp"
#include "BombWorld.hpp"
#include "BombPickup.hpp"
#include "BombWheelPickup.hpp"
#include "BombSignals.hpp"
#include "BombVisualBackend.hpp"
#include "BombRenderPolicy.hpp"
#include "totk/engine/AbilityChoice.hpp"
#include "totk/engine/Npad.hpp"
#include <lib.hpp>
#include <atomic>
#include <cmath>

namespace remote_bomb::runtime {
namespace eng = totk::engine;
namespace {
// Confirms a granted Materials item stays in the native pouch while the form is Stored.
struct StockWatch {
    std::uint32_t serial{};
    unsigned checks{}, missing{}, repairs{};
    std::array<inventory::Status, 2> logged{};
    bool halted{};
};
// Last highlight decision per bomb, logged when it changes.
struct HighlightTrace {
    std::uint32_t serial{};
    highlight::Level level{};
    bool active{}, focused{}, held{};
};
// Last hold state per bomb seen with Link close by, for the pickup diagnostic.
struct PickupTrace {
    std::uint32_t serial{};
    std::uint64_t flags{};
    bool carried{};
};
// The player's Ultrahand fields the highlight reads.
struct Hand {
    bool valid{};
    unsigned mode{}, focus{}, link{};
};
// An exploded bomb whose body the game has not removed yet.
struct Spent {
    engine::Handle actor;
    std::uint32_t serial{};
    float frames{};
};
struct Runtime {
    std::uintptr_t base{};
    engine::Bindings native;
    engine::Signals signals;
    State gameplay;
    effects::Timeline effects;
    input::State controls;
    eng::SceneTracker tracker;
    eng::SceneContext scene;
    eng::NpadReader controller;
    engine::Handle player;
    engine::Roster roster;
    std::array<engine::Handle, 2> actors{};
    std::array<float, 2> detonationFrames{};
    std::array<float, 2> inventoryRetryFrames{};
    std::array<StockWatch, 2> stock{};
    float stockFrames{};
    std::array<engine::Handle, 8> retired{};
    std::array<Spent, 2> spent{};
    std::array<HighlightTrace, 2> highlights{};
    unsigned highlightLogs{};
    std::array<PickupTrace, 2> pickups{};
    unsigned pickupLogs{};
    bool pickupPress{};
    bool modChosen{};
    std::atomic<std::uint64_t> wheelRequest{};
    pickup::Pending wheelPickup;
    std::uint64_t priorClock{};
    unsigned generation{}, seenSelections{};
    totk::engine::AbilityChoiceWatch otherAbilities;
    std::atomic_flag tickLock = ATOMIC_FLAG_INIT;
    std::atomic_flag presentationLock = ATOMIC_FLAG_INIT;
    Presentation frame;
} state;
void apply(Input input) {
    state.gameplay = transition(state.gameplay, input).next;
    if (input.event == Event::SceneChanged)
        state.effects = {};
}
void beginEffect(Shape shape, engine::Handle actor, Ticket ticket) {
    const auto p = engine::position(actor);
    state.effects = effects::begin(state.effects, shape, {p.x, p.y, p.z}, ticket);
    Logging.Log("[remote-bomb] FX_BEGIN form=%u serial=%u position=%f,%f,%f", index(shape), ticket.serial,
                double(p.x), double(p.y), double(p.z));
}
engine::Actor borrow(engine::Handle handle) {
    return {reinterpret_cast<void *>(handle.address), state.generation};
}
bool current(engine::Handle handle) {
    return state.roster.contains(handle) && handle.isCurrent(state.scene.token);
}
void invalidateScene(const char *reason) {
    state.wheelRequest.store(0,std::memory_order_release);
    state.wheelPickup = {};
    if (!state.scene.token.isValid())
        return;
    Logging.Log("[remote-bomb] SCENE_RESET reason=%s generation=%u", reason, state.generation);
    if (++state.generation == 0)
        ++state.generation;
    apply({Event::SceneChanged, {}, {}, state.generation});
    state.scene = {};
    state.player = {};
    state.roster = {};
    state.actors = {};
    state.detonationFrames = {};
    state.inventoryRetryFrames = {};
    for (auto &watch : state.stock)
        watch = {0, 0, 0, watch.repairs, {}, watch.halted};
    state.stockFrames = 0.f;
    state.retired = {};
    state.spent = {};
    state.controls.tap = false;
    state.signals.player.store(0);
}
bool refreshScene() {
    const auto snapshot = state.tracker.observe(eng::resolveScene(state.base));
    if (!snapshot.context.isReady()) {
        invalidateScene("world unavailable");
        return false;
    }
    if (state.scene.token != snapshot.context.token) {
        invalidateScene("new scene");
        if (++state.generation == 0)
            ++state.generation;
        apply({Event::SceneChanged, {}, {}, state.generation});
        state.scene = snapshot.context;
        Logging.Log("[remote-bomb] SCENE_READY generation=%u scene=%p", state.generation,
                    reinterpret_cast<void *>(state.scene.token.value));
    }
    const auto players = eng::resolvePlayerActors(state.scene);
    if (!players) {
        Logging.Log("[remote-bomb] PLAYER_UNAVAILABLE error=%u", unsigned(players.error));
        invalidateScene("player unavailable");
        return false;
    }
    state.player = players.value.player;
    state.signals.player.store(state.player.address, std::memory_order_release);
    return true;
}
bool isRetired(engine::Handle actor) {
    for (const auto old : state.retired)
        if (old.address == actor.address && old.capturedNamePointer == actor.capturedNamePointer)
            return true;
    return false;
}
bool retire(engine::Handle actor) {
    if (!actor.address || isRetired(actor))
        return true;
    for (auto &old : state.retired)
        if (!old.address) {
            old = actor;
            return true;
        }
    Logging.Log("[remote-bomb] RETIRE_REFUSED actor=%p capacity=%u",
                reinterpret_cast<void *>(actor.address), unsigned(state.retired.size()));
    return false;
}
bool claimed(engine::Handle actor) {
    if (isRetired(actor))
        return true;
    for (const auto owned : state.actors)
        if (owned.address == actor.address && owned.capturedNamePointer == actor.capturedNamePointer)
            return true;
    return false;
}
void adoptInventoryActor(unsigned form) {
    const auto phase = state.gameplay.slots[form].phase;
    if (phase != Phase::Ready && phase != Phase::Stored)
        return;
    const auto shape = form ? Shape::Cube : Shape::Sphere;
    for (unsigned i = 0; i < state.roster.count; ++i) {
        const auto entry = state.roster.entries[i];
        if (entry.shape != shape || entry.upgrade != state.gameplay.upgrade ||
            entry.processState != 4 || claimed(entry.actor) || !current(entry.actor))
            continue;
        state.actors[form] = entry.actor;
        if (!nativeBomb(shape))
            physics::queueRefresh(entry.actor);
        apply({Event::InventoryActorCreated, shape});
        const auto ticket = state.gameplay.slots[form].ticket;
        beginEffect(shape, entry.actor, ticket);
        state.controls.remembered = true;
        state.controls.reclaimed = false;
        apply({shape == Shape::Sphere ? Event::SelectSphere : Event::SelectCube});
        Logging.Log("[remote-bomb] INVENTORY_ACTOR_ADOPTED form=%u serial=%u actor=%p", form,
                    ticket.serial, reinterpret_cast<void *>(entry.actor.address));
        return;
    }
}
// The game removes an exploded bomb about 0.3 s after the blast; one still alive a second later is removed here so its body never lingers.
constexpr float kSpentFrames = 30.f;
void removeSpent(unsigned form, float frames) {
    auto &spent = state.spent[form];
    if (!spent.actor.address)
        return;
    const engine::RosterEntry *entry = nullptr;
    for (unsigned i = 0; i < state.roster.count; ++i)
        if (state.roster.entries[i].actor.address == spent.actor.address)
            entry = &state.roster.entries[i];
    if (!entry || entry->processState != 4 || !current(spent.actor)) {
        spent = {};
        return;
    }
    if ((spent.frames += frames) < kSpentFrames)
        return;
    Logging.Log("[remote-bomb] SPENT_REMOVED form=%u serial=%u frames=%f actor=%p", form, spent.serial,
                double(spent.frames), reinterpret_cast<void *>(spent.actor.address));
    engine::discard(state.native, state.generation, borrow(spent.actor));
    spent = {};
}
void observeActors(float frames) {
    state.roster = engine::scan(state.base, state.scene);
    if (!state.roster.complete)
        return;
    for (auto &old : state.retired)
        if (old.address && !state.roster.contains(old))
            old = {};
    for (unsigned form = 0; form < 2; ++form) {
        auto &actor = state.actors[form];
        const auto shape = form ? Shape::Cube : Shape::Sphere;
        const auto slot = state.gameplay.slots[form];
        if (nativeBomb(shape) && actor.address) {
            bool active = false;
            for (unsigned i = 0; i < state.roster.count; ++i)
                active |= state.roster.entries[i].actor.address == actor.address &&
                          state.roster.entries[i].processState == 4;
            if (!active)
                for (unsigned i = 0; i < state.roster.count; ++i) {
                    const auto &next = state.roster.entries[i];
                    if (next.shape == shape && next.upgrade == state.gameplay.upgrade &&
                        next.processState == 4 && current(next.actor) && !isRetired(next.actor)) {
                        Logging.Log("[remote-bomb] NATIVE_BOMB_HANDOFF old=%p actor=%p",
                                    reinterpret_cast<void *>(actor.address), reinterpret_cast<void *>(next.actor.address));
                        actor = next.actor;
                        break;
                    }
                }
        }
        if (actor.address && !current(actor)) {
            const bool detonated = nativeBomb(shape) && slot.phase == Phase::DetonationRequested;
            apply({detonated ? Event::Exploded : Event::ActorLost, shape, slot.ticket});
            if (detonated)
                Logging.Log("[remote-bomb] NATIVE_BOMB_DEATH_COMPLETE serial=%u recharge=%f", slot.ticket.serial,
                            double(state.gameplay.slots[form].rechargeSeconds));
            Logging.Log("[remote-bomb] ACTOR_LOST form=%u phase=%u actor=%p", form,
                        unsigned(slot.phase), reinterpret_cast<void *>(actor.address));
            actor = {};
        } else if (actor.address && (slot.phase == Phase::Live || slot.phase == Phase::DetonationRequested) &&
                   !nativeBomb(shape) &&
                   engine::explosionReported(state.native, state.generation, borrow(actor))) {
            state.spent[form] = {actor, slot.ticket.serial, 0.f};
            apply({Event::Exploded, shape, slot.ticket});
            Logging.Log("[remote-bomb] EXPLODED form=%u serial=%u recharge=%f", form, slot.ticket.serial,
                        double(state.gameplay.slots[form].rechargeSeconds));
        } else if (slot.phase == Phase::DetonationRequested && frames > 0.f) {
            state.detonationFrames[form] += frames;
            if (state.detonationFrames[form] >= 30.f) {
                apply({Event::DetonationFailed, shape, slot.ticket});
                Logging.Log("[remote-bomb] DETONATE_TIMEOUT form=%u serial=%u frames=%f retryable=1",
                            form, slot.ticket.serial, double(state.detonationFrames[form]));
            }
        }
        removeSpent(form, frames);
        if (frames > 0.f)
            adoptInventoryActor(form);
    }
}
bool worldBomb(Shape shape) {
    for (unsigned i = 0; i < state.roster.count; ++i)
        if (state.roster.entries[i].shape == shape && state.roster.entries[i].upgrade == state.gameplay.upgrade)
            return true;
    return false;
}
// Both readers are probed every 10 frames. A form missing for 3 checks together with the other form is a whole-pouch replacement (save load); a single form needs 9 checks (about 3 s).
constexpr float kStockCheckFrames = 10.f;
constexpr unsigned kReplacedChecks = 3, kLostChecks = 9;
bool watchStock(unsigned form, bool check) {
    auto &watch = state.stock[form];
    const auto slot = state.gameplay.slots[form];
    if (slot.phase != Phase::Stored)
        return false;
    if (watch.serial != slot.ticket.serial) {
        watch.serial = slot.ticket.serial;
        watch.checks = watch.missing = 0;
    }
    if (!check)
        return false;
    const auto shape = form ? Shape::Cube : Shape::Sphere;
    const auto queued = inventory::probe(state.base, shape, state.gameplay.upgrade, false);
    const auto committed = inventory::probe(state.base, shape, state.gameplay.upgrade, true);
    if (!watch.checks++ || queued.status != watch.logged[0] || committed.status != watch.logged[1])
        Logging.Log("[remote-bomb] INVENTORY_CONFIRM form=%u serial=%u check=%u queued=%u slot=%d quantity=%d "
                    "committed=%u slot=%d quantity=%d failed=%u stage=%u", form, slot.ticket.serial, watch.checks,
                    unsigned(queued.status), queued.slot, queued.quantity, unsigned(committed.status),
                    committed.slot, committed.quantity, committed.failed, committed.stage);
    watch.logged = {queued.status, committed.status};
    if (queued.status == inventory::Status::Present || committed.status == inventory::Status::Present) {
        watch.missing = watch.repairs = 0;
        watch.halted = false;
        return false;
    }
    if (queued.status != inventory::Status::Missing || committed.status != inventory::Status::Missing ||
        worldBomb(shape)) {
        watch.missing = 0;
        return false;
    }
    return ++watch.missing >= kReplacedChecks;
}
void repairStock(unsigned form, bool replaced) {
    auto &watch = state.stock[form];
    const auto slot = state.gameplay.slots[form];
    if (!replaced && watch.missing < kLostChecks)
        return;
    if (watch.repairs >= 2) {
        if (!watch.halted)
            Logging.Log("[remote-bomb] INVENTORY_REPAIR_HALTED form=%u serial=%u repairs=%u", form,
                        slot.ticket.serial, watch.repairs);
        watch.halted = true;
        return;
    }
    ++watch.repairs;
    apply({replaced ? Event::StockReplaced : Event::StockLost, form ? Shape::Cube : Shape::Sphere, slot.ticket});
    Logging.Log("[remote-bomb] INVENTORY_%s form=%u serial=%u checks=%u repair=%u recharge=%f",
                replaced ? "REPLACED" : "LOST", form, slot.ticket.serial, watch.checks, watch.repairs,
                double(state.gameplay.slots[form].rechargeSeconds));
}
// Multi-shot bows fuse one copy per arrow; quick L sets off every copy of the form. Copies are retired first so one is never adopted as the next inventory bomb.
unsigned detonateCopies(Shape shape) {
    unsigned count = 0;
    for (unsigned i = 0; i < state.roster.count; ++i) {
        const auto entry = state.roster.entries[i];
        if (entry.shape != shape || entry.upgrade != state.gameplay.upgrade || entry.processState != 4 ||
            claimed(entry.actor) || !current(entry.actor) || !retire(entry.actor))
            continue;
        const auto route = nativeBomb(shape) ? engine::DetonationRoute::DeathDrop : engine::DetonationRoute::Chemical;
        if (engine::requestDetonation(state.native, state.generation, borrow(entry.actor), route) == engine::Status::Accepted)
            ++count;
    }
    return count;
}
void execute(Command command, bool workerAdvanced) {
    const auto form = index(command.shape);
    if (command.effect == Effect::Create) {
        if (!state.roster.complete || !retire(state.actors[form])) {
            apply({Event::CreationFailed, command.shape, command.ticket});
            return;
        }
        state.actors[form] = {};
        const auto status = inventory::ensure(state.base, command.shape, state.gameplay.upgrade,
                                               workerAdvanced);
        const bool accepted = status == inventory::Status::Granted || status == inventory::Status::Present;
        state.inventoryRetryFrames[form] = accepted ? 0.f : 30.f;
        apply({accepted ? Event::Provisioned : Event::CreationFailed, command.shape, command.ticket});
    } else if (command.effect == Effect::Detonate) {
        state.detonationFrames[form] = 0.f;
        const auto actor = state.actors[form];
        if (!state.roster.complete || !current(actor)) {
            apply({Event::DetonationFailed, command.shape, command.ticket});
            Logging.Log("[remote-bomb] DETONATE_REFUSED form=%u roster_complete=%u actor=%p", form,
                        unsigned(state.roster.complete), reinterpret_cast<void *>(actor.address));
            return;
        }
        const auto route = nativeBomb(command.shape) ? engine::DetonationRoute::DeathDrop : engine::DetonationRoute::Chemical;
        const auto status = engine::requestDetonation(state.native, state.generation, borrow(actor), route);
        if (status != engine::Status::Accepted)
            apply({Event::DetonationFailed, command.shape, command.ticket});
        const auto copies = detonateCopies(command.shape);
        Logging.Log("[remote-bomb] DETONATE form=%u serial=%u status=%u copies=%u route=%u", form, command.ticket.serial,
                    unsigned(status), copies, unsigned(route));
    }
}
// The bomb's Ultrahand receiver (actor component table -> receiver), or 0.
std::uintptr_t receiverOf(std::uintptr_t actor) {
    const auto table = actor ? eng::readMemory<std::uintptr_t>(actor + 0x228) : 0;
    const auto receiver = eng::isPlausibleAddress(table) ? eng::readMemory<std::uintptr_t>(table + 0x480) : 0;
    return eng::isPlausibleAddress(receiver) ? receiver : 0;
}
// Receiver state bit seen only while Link holds the bomb from the Materials menu; the lifter's held reference does not report that hold.
constexpr std::uint64_t kMaterialHoldFlag = 0x10000000;
// The player's Ultrahand (actor component table -> SpecialPower -> UltraHand), or 0.
std::uintptr_t ultraHandOf(std::uintptr_t player) {
    const auto table = player ? eng::readMemory<std::uintptr_t>(player + 0x228) : 0;
    const auto power = eng::isPlausibleAddress(table) ? eng::readMemory<std::uintptr_t>(table + 0x478) : 0;
    const auto hand = eng::isPlausibleAddress(power) ? eng::readMemory<std::uintptr_t>(power + 0x28) : 0;
    return eng::isPlausibleAddress(hand) ? hand : 0;
}
// +8 is 1 while Ultrahand is out; +184 is the aimed actor's id, with its link id at +208 (-1 when nothing is aimed at).
Hand readHand(std::uintptr_t hand) {
    if (!hand)
        return {};
    return {true, eng::readMemory<unsigned char>(hand + 8), eng::readMemory<unsigned>(hand + 184),
            eng::readMemory<unsigned>(hand + 208)};
}
highlight::Level highlightOf(unsigned form, const Hand &hand, engine::Position bomb) {
    const auto actor = state.actors[form].address;
    const auto receiver = receiverOf(actor);
    const auto flags = receiver ? eng::readMemory<std::uint64_t>(receiver + 32) : 0;
    const auto player = engine::position(state.player);
    const float dx = bomb.x - player.x, dy = bomb.y - player.y, dz = bomb.z - player.z;
    highlight::Inputs in{hand.valid && hand.mode == 1,
                         hand.valid && hand.link != 0xffffffffu && hand.focus == eng::readMemory<unsigned>(actor + 0x3F0),
                         flags, std::sqrt(dx * dx + dy * dy + dz * dz)};
    const auto level = highlight::decide(in);
    auto &trace = state.highlights[form];
    const auto serial = state.gameplay.slots[form].ticket.serial;
    const bool held = highlight::held(flags);
    if (trace.serial == serial && trace.level == level && trace.active == in.active && trace.focused == in.focused &&
        trace.held == held)
        return level;
    trace = {serial, level, in.active, in.focused, held};
    if (state.highlightLogs < 160) {
        ++state.highlightLogs;
        Logging.Log("[remote-bomb] HIGHLIGHT form=%u serial=%u level=%u active=%u focused=%u held=%u "
                    "receiver_flags=%llx distance=%f",
                    form, serial, unsigned(level), unsigned(in.active), unsigned(in.focused), unsigned(held),
                    (unsigned long long)flags, double(in.distance));
    }
    return level;
}
void serviceWheelPickup(const eng::NpadFrame& frame, bool running, float frames) {
    using pickup::Stage;
    auto& pending = state.wheelPickup;
    const auto wheel = input::unpack(state.signals.wheel.load(std::memory_order_acquire));
    const auto requested = state.wheelRequest.load(std::memory_order_acquire);
    if (requested && !pickup::belongs(requested,wheel.epoch)) state.wheelRequest.store(0);
    if (requested && pickup::belongs(requested,wheel.epoch) && wheel.closed && !wheel.open && running) {
        state.wheelRequest.store(0);
        if (!pending.active()) {
            pending = {Stage::Waiting, unsigned(requested)==2 ? Shape::Cube : Shape::Sphere,0,wheel.epoch};
            pending.inheritedButtons = pickup::actionButtons(frame.snapshot().buttons);
            Logging.Log("[remote-bomb] WHEEL_PICKUP_REQUEST form=%u epoch=%u buttons=%llx",
                index(pending.shape),wheel.epoch,(unsigned long long)frame.snapshot().buttons);
            apply({pending.shape==Shape::Cube ? Event::SelectCube : Event::SelectSphere});
            state.controls.remembered = true;
            state.controls.reclaimed = state.controls.tap = false;
            state.modChosen = false;
            state.signals.remembered.store(true,std::memory_order_release);
            eng::chooseAbility(eng::Ability::RemoteBomb);
        }
    }
    if (!pending.active() || !running || !frame.snapshot().freshSampleCount) return;
    const bool expired = pending.advance(frames);
    if (pending.stage == Stage::Refund) {
        if (inventory::restoreOne(state.base,pending.shape,state.gameplay.upgrade,frames>0)==inventory::Status::Granted)
            pending = {};
        else if (expired) {
            Logging.Log("[remote-bomb] WHEEL_REFUND_DEFERRED form=%u",index(pending.shape));
            pending = {}; // The ordinary stock watcher will reconcile the missing form.
        }
        return;
    }
    const auto sample = frame.snapshot();
    if (expired || wheel.open || wheel.epoch != pending.epoch ||
        pickup::cancelInput(pending,sample.buttons)) {
        Logging.Log("[remote-bomb] WHEEL_PICKUP_CANCEL form=%u stage=%u frames=%f buttons=%llx epoch=%u open=%u",
                    index(pending.shape),unsigned(pending.stage),double(pending.frames),
                    (unsigned long long)sample.buttons,wheel.epoch,unsigned(wheel.open));
        pending = {}; // An already-created bomb remains a normal usable world bomb.
        return;
    }
    const auto form = index(pending.shape);
    const auto held = engine::carriedActor(state.native,state.generation,borrow(state.player)).value;
    const auto bomb = state.actors[form];
    if (held) {
        Logging.Log("[remote-bomb] WHEEL_PICKUP_HELD form=%u intended=%u",form,
                    unsigned(bomb.address && held==borrow(bomb).value));
        pending = {};
        return;
    }
    if (pickup::actionButtons(sample.buttons) || !pickup::ready(state.base,state.player))
        return;
    if (pending.stage == Stage::Waiting) {
        const auto phase = state.gameplay.slots[form].phase;
        if (phase == Phase::Live) pending.stage = Stage::Attention;
        else if (phase == Phase::Stored && frames>0) {
            const auto taken = inventory::takeOne(state.base,pending.shape,state.gameplay.upgrade,true);
            if (taken == inventory::TakeStatus::Deferred) return;
            if (taken == inventory::TakeStatus::Uncertain) {
                Logging.Log("[remote-bomb] WHEEL_DEBIT_UNCERTAIN form=%u",form);
                pending = {}; // Never repeat a debit whose read-back failed.
                return;
            }
            pending.stage = pickup::spawn(state.base,state.player,pending.shape,state.gameplay.upgrade) ?
                Stage::Creating : Stage::Refund;
            return;
        } else if (phase==Phase::Recharging || phase==Phase::DetonationRequested) {
            Logging.Log("[remote-bomb] WHEEL_PICKUP_UNAVAILABLE form=%u phase=%u",form,unsigned(phase));
            pending = {};
            return;
        }
    }
    if (pending.stage==Stage::Creating && bomb.address && current(bomb)) pending.stage=Stage::Attention;
    if (pending.stage==Stage::Attention && bomb.address && current(bomb) &&
        pickup::focused(state.base,state.player,bomb)) {
        // One native A edge enters PlayerPreLift/PlayerLift, owning animation and controls.
        // No Lifter attachment, forged action node or hand-parenting is used.
        frame.addOwnedButtons(1u);
        pending.stage=Stage::Pressed;
        Logging.Log("[remote-bomb] WHEEL_PICKUP_PRESS form=%u actor=%p",form,
                    reinterpret_cast<void*>(bomb.address));
    }
}
// Log native pickup transitions near a bomb; Ultrahand's per-frame bits are ignored.
void tracePickup(unsigned form, engine::Position bomb) {
    if (!state.player.address || state.pickupLogs >= 80)
        return;
    const auto player = engine::position(state.player);
    const float dx = bomb.x - player.x, dy = bomb.y - player.y, dz = bomb.z - player.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance > 3.f)
        return;
    const auto receiver = receiverOf(state.actors[form].address);
    const auto flags = (receiver ? eng::readMemory<std::uint64_t>(receiver + 32) : 0) & ~std::uint64_t(0xC000);
    const bool carried = engine::carriedActor(state.native, state.generation, borrow(state.player)).value ==
                         borrow(state.actors[form]).value;
    auto &trace = state.pickups[form];
    const auto serial = state.gameplay.slots[form].ticket.serial;
    if (!state.pickupPress && trace.serial == serial && trace.flags == flags && trace.carried == carried)
        return;
    trace = {serial, flags, carried};
    ++state.pickupLogs;
    Logging.Log("[remote-bomb] PICKUP_TRACE form=%u serial=%u a_pressed=%u carried=%u receiver_flags=%llx "
                "distance=%f remembered=%u", form, serial, unsigned(state.pickupPress), unsigned(carried),
                (unsigned long long)flags, double(distance), unsigned(state.controls.remembered));
}
void publish(bool running) {
    Presentation frame;
    frame.state = state.gameplay;
    frame.available = state.scene.isReady() && state.player.address;
    frame.remembered = state.controls.remembered;
    frame.running = running;
    frame.clock = state.priorClock;
    const auto hand = state.player.address ? readHand(ultraHandOf(state.player.address)) : Hand{};
    for (unsigned form = 0; form < 2; ++form) {
        const auto phase = state.gameplay.slots[form].phase;
        if (!state.roster.complete || !current(state.actors[form]) ||
            (phase != Phase::Live && phase != Phase::DetonationRequested)) {
            state.effects = effects::track(state.effects, form ? Shape::Cube : Shape::Sphere, {}, {}, false);
            continue;
        }
        auto &view = frame.bombs[form];
        view.position = engine::position(state.actors[form]);
        view.rotation = render_policy::drawRotation(
            eng::readMemory<std::array<float, 9>>(state.actors[form].address + eng::layout::kActorRotation));
        view.live = true;
        view.highlight = highlightOf(form, hand, view.position);
        tracePickup(form, view.position);
        state.effects = effects::track(state.effects, form ? Shape::Cube : Shape::Sphere,
                                      state.gameplay.slots[form].ticket,
                                      {view.position.x, view.position.y, view.position.z}, true);
    }
    frame.effects = state.effects;
    if (!state.presentationLock.test_and_set(std::memory_order_acquire)) {
        state.frame = frame;
        state.presentationLock.clear(std::memory_order_release);
    }
}
}
bool initialize(std::uintptr_t base) {
    engine::BindingFailure failure;
    if (!engine::validateRuntime(base) || !pickup::validate(base) ||
        !engine::bindTotk121(base, state.native, &failure)) {
        Logging.Log("[remote-bomb] BIND_REFUSED error=%u offset=%p expected=%016llx actual=%016llx",
                    unsigned(failure.error), reinterpret_cast<void *>(failure.offset),
                    (unsigned long long)failure.expected, (unsigned long long)failure.actual);
        return false;
    }
    state.base = base;
    state.gameplay.upgrade = REMOTE_BOMB_UPGRADED ? Upgrade::Plus : Upgrade::Base;
    Logging.Log("[remote-bomb] INITIALIZED upgrade=%u damage=%u recharge=%f", unsigned(state.gameplay.upgrade),
                abilityProfile(state.gameplay.upgrade).damage, double(abilityProfile(state.gameplay.upgrade).rechargeSeconds));
    return true;
}
void tick(void *device) {
    if (state.tickLock.test_and_set(std::memory_order_acquire))
        return;
    struct Unlock { ~Unlock() { state.tickLock.clear(std::memory_order_release); } } unlock;
    const auto frame = state.controller.read(device);
    const auto clock = state.signals.clock.load(std::memory_order_acquire);
    const bool running = state.signals.running.load(std::memory_order_acquire);
    const float frames = float(double(clock - state.priorClock) / 4294967296.0);
    state.priorClock = clock;
    const bool available = refreshScene();
    engine::explosion::sceneChanged(available ? state.generation : 0);
    engine::explosion::publishPlayer(available ? state.player.address : 0);
    if (available) {
        if (running) {
            state.gameplay = advanceRecharge(state.gameplay, frames / 30.f);
            state.effects = effects::advance(state.effects, frames);
            for (auto &retry : state.inventoryRetryFrames)
                retry = std::max(0.f, retry - frames);
        }
        observeActors(running ? frames : 0.f);
    }

    const auto snapshot = frame.snapshot();
    const bool fresh = snapshot.freshSampleCount != 0;
    const auto buttons = fresh ? snapshot.buttons : state.controls.lastButtons;
    state.pickupPress = fresh && (buttons & ~state.controls.lastButtons & 1u);  // A
    const auto selections = state.signals.nativeSelections.load(std::memory_order_acquire);
    const bool otherChosen = state.otherAbilities.chosenElsewhere(totk::engine::Ability::RemoteBomb);
    if (selections != state.seenSelections)
        state.modChosen = false;
    if (otherChosen)
        state.modChosen = true;
    bool bombOut{};
    for (const auto &slot : state.gameplay.slots)
        bombOut |= available && (slot.phase == Phase::Live || slot.phase == Phase::DetonationRequested);
    const auto controls = input::transition(state.controls,
        {input::unpack(state.signals.wheel.load(std::memory_order_acquire)), buttons,
         fresh ? buttons & ~state.controls.lastButtons : 0, fresh, running, available,
         selections != state.seenSelections || otherChosen, bombOut, state.modChosen});
    state.seenSelections = selections;
    // A bomb left out answers the next L press, so the other abilities leave that press alone.
    totk::engine::holdL(totk::engine::Ability::RemoteBomb, controls.next.reclaimed || bombOut);
    if (state.controls.reclaimed && !controls.next.reclaimed)
        Logging.Log("[remote-bomb] L_RETURNED to_mod=%u", unsigned(controls.next.handing));
    state.controls = controls.next;
    state.signals.remembered.store(input::blocksNative(controls.next), std::memory_order_release);
    const bool wheelAction = state.wheelRequest.load(std::memory_order_acquire) || state.wheelPickup.active();
    if (available) serviceWheelPickup(frame,running,frames);
    if (controls.event != input::Event::None)
        Logging.Log("[remote-bomb] INPUT event=%u remembered=%u", unsigned(controls.event),
                    unsigned(controls.next.remembered));
    if (available) {
        const auto target = detonationTarget(state.gameplay);
        // The game holds a blast request while Link has the bomb in his hands and plays it once he sets the bomb down, so quick L does nothing while he is lifting or holding it.
        const auto bomb = state.actors[index(target)];
        const bool lifted = controls.use && running && bomb.address &&
            engine::carriedActor(state.native, state.generation, borrow(state.player)).value == borrow(bomb).value;
        const auto receiver = controls.use && running ? receiverOf(bomb.address) : 0;
        const auto flags = receiver ? eng::readMemory<std::uint64_t>(receiver + 32) : 0;
        const bool held = lifted || (flags & kMaterialHoldFlag);
        if (held)
            Logging.Log("[remote-bomb] DETONATE_HELD form=%u serial=%u lifted=%u receiver_flags=%llx",
                        unsigned(index(target)), state.gameplay.slots[index(target)].ticket.serial,
                        unsigned(lifted), (unsigned long long)flags);
        if (controls.use && !wheelAction && running && !held && state.gameplay.slots[index(target)].phase == Phase::Live) {
            apply({target == Shape::Sphere ? Event::SelectSphere : Event::SelectCube});
            const auto action = transition(state.gameplay, {Event::UseSelected});
            state.gameplay = action.next;
            execute(action.command, running && frames > 0.f);
        }
    }
    if (available && running && frames > 0.f && state.roster.complete) {
        const auto selected = state.gameplay.selected;
        state.stockFrames += frames;
        const bool check = state.stockFrames >= kStockCheckFrames;
        if (check)
            state.stockFrames = 0.f;
        std::array<bool, 2> missing{};
        for (unsigned form = 0; form < 2; ++form)
            missing[form] = state.wheelPickup.active() && index(state.wheelPickup.shape)==form ?
                false : watchStock(form, check);
        for (unsigned form = 0; form < 2; ++form)
            if (missing[form])
                repairStock(form, missing[0] && missing[1]);
        for (unsigned form = 0; form < 2; ++form) {
            if (state.gameplay.slots[form].phase != Phase::Ready || state.inventoryRetryFrames[form] > 0.f)
                continue;
            apply({form ? Event::SelectCube : Event::SelectSphere});
            const auto action = transition(state.gameplay, {Event::UseSelected});
            state.gameplay = action.next;
            execute(action.command, true);
        }
        apply({selected == Shape::Sphere ? Event::SelectSphere : Event::SelectCube});
    }
    publish(running);
}
bool presentation(Presentation &out) {
    if (state.presentationLock.test_and_set(std::memory_order_acquire))
        return false;
    out = state.frame;
    state.presentationLock.clear(std::memory_order_release);
    return out.available;
}
void publishPhysicsTime(float frames) {
    engine::publishPhysicsTime(state.base, state.signals, frames);
    engine::explosion::publishTime(state.signals.clock.load(std::memory_order_acquire),
                                   state.signals.running.load(std::memory_order_acquire));
}
void wheelOpened(std::uintptr_t screen) { engine::wheelOpened(state.signals, screen); }
bool chooseFromWheel(std::uintptr_t screen, Shape shape) {
    const auto wheel=input::unpack(state.signals.wheel.load(std::memory_order_acquire));
    if (!state.signals.player.load(std::memory_order_acquire) || !wheel.open || wheel.exited ||
        state.signals.wheelOwner.load(std::memory_order_acquire)!=screen) return false;
    std::uint64_t empty=0;
    return state.wheelRequest.compare_exchange_strong(empty,pickup::request(wheel.epoch,shape),
                                                     std::memory_order_acq_rel);
}
void wheelExited(std::uintptr_t screen, bool cancelled) { engine::wheelExited(state.signals, screen, cancelled); }
void wheelClosed(std::uintptr_t screen) { engine::wheelClosed(state.signals, screen); }
void nativeSelectionAccepted(std::uintptr_t screen) { engine::nativeSelectionAccepted(state.signals, screen); }
void maskRecentAbilityKey(std::uintptr_t node) { engine::maskRecentAbilityKey(state.base, state.signals, node); }
std::uint64_t nativeAbilityAdmission(std::uint64_t (*original)(std::uintptr_t), std::uintptr_t actor,
                                    std::uintptr_t caller) {
    return engine::nativeAbilityAdmission(original, state.signals, actor, caller - state.base);
}
void cancelOwnedAmiibo(std::uintptr_t node) { engine::cancelOwnedAmiibo(state.base, state.signals, node); }
}
