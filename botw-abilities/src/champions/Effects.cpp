// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Effects.hpp"
#include "Appearances.hpp"
#include "EffectPulse.hpp"
#include "FuryField.hpp"
#include "NativeField.hpp"
#include "GaleResources.hpp"
#include "WindFields.hpp"
#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/CodeCheck.hpp"
#include "totk/engine/Transform.hpp"
#include <algorithm>
#include <lib.hpp>

namespace botw_abilities::champions::effects {
namespace {
using namespace totk::engine;
constexpr CodeSite kSites[]{
    {0xAB92CC, 0xA9BA7BFD, 0xA9016FFC}, {0x83AA68, 0xD10143FF, 0xA9017BFD},
    {0x6BA86C, 0xA9BD7BFD, 0xF9000BF5},
};
std::uintptr_t g_base{};
constexpr std::size_t kWindFirst = kEffectActors.size(), kWindCount = 3;
constexpr std::size_t kCarrierCount = kWindFirst + kWindCount;
constexpr std::size_t kMaxWindActors = 16, kMaxDuplicates = 64;
std::array<CarrierState, kCarrierCount> g_slots{};
std::array<totk::core::WorldTransform, kCarrierCount> g_fixed{};
std::array<bool, kCarrierCount> g_hasFixed{};
std::array<std::uint32_t, kWindCount> g_windIds{};
struct FuryBolt {
    totk::core::WorldPosition position{};
    std::uint32_t id{};
    float remaining{};
    bool waiting = false;
};
std::array<FuryBolt,64> g_bolts{};

template <class Fn> Fn call(std::uintptr_t offset) { return reinterpret_cast<Fn>(g_base + offset); }

struct CreateArgument {
    float position[3], rotation[9], scale[3];
    const char* name;
    unsigned char opaque48[0x10];
    void *blackboard, *opaque60, *parent, *dependent, *creator, *rtti, *heap;
    unsigned char flags, padding[7];
};
static_assert(sizeof(CreateArgument) == 0x98);
static_assert(offsetof(CreateArgument, flags) == 0x90);

bool isWind(std::size_t index) { return index >= kWindFirst; }
const char* name(std::size_t index) {
    if (index < kWindFirst) return kEffectActors[index];
    return kGaleUpdraftActor;
}

bool spawnActor(const char* actor, const totk::core::WorldTransform& transform, const std::array<float,3>& scale) {
    const auto manager = readMemory<std::uintptr_t>(g_base + 0x4722920);
    if (!isPlausibleAddress(manager)) {
        Logging.Log("[champions] EFFECT_SPAWN_REFUSED actor=%s reason=manager", actor);
        return false;
    }
    CreateArgument argument{};
    std::memcpy(argument.position, &transform.position, sizeof(argument.position));
    std::memcpy(argument.rotation, transform.rotation.values, sizeof(argument.rotation));
    std::copy(scale.begin(),scale.end(),argument.scale);
    argument.name = "";
    argument.flags = 7;
    std::uint32_t result{};
    void* preactor{};
    using Create = bool(*)(std::uintptr_t, const char**, const CreateArgument*, void*, unsigned,
                          void*, void*, void*, bool, unsigned*, void**);
    const bool accepted = call<Create>(0xAB92CC)(manager, &actor, &argument, nullptr, 1,
        nullptr, nullptr, nullptr, false, &result, &preactor);
    Logging.Log("[champions] EFFECT_SPAWN actor=%s accepted=%u result=%u", actor, unsigned(accepted), result);
    return accepted;
}

// Shells centre on Link's chest; Sidon's own shell is bound to the Spine_1 bone, about 1 m up.
bool chestHeight(std::size_t effect) {
    return effect == static_cast<std::size_t>(Effect::DarukShell) ||
           effect == static_cast<std::size_t>(Effect::DarukShellBreak);
}

bool spawn(std::size_t effect, const totk::core::WorldTransform& transform) {
    auto placement = transform;
    if (chestHeight(effect)) placement.position.y += 1.0f;
    return spawnActor(name(effect),placement,isWind(effect) ? kGaleUpdraftScale : std::array<float,3>{1,1,1});
}

void remove(ActorHandle actor) {
    call<void(*)(std::uintptr_t)>(0x83AA68)(actor.address);
}

struct CarrierRoster {
    std::array<ActorHandle, kCarrierCount> actors{};
    std::array<ActorHandle, kMaxWindActors> winds{};
    std::size_t windCount = 0;
    std::array<ActorHandle, kMaxDuplicates> duplicates{};
    std::size_t duplicateCount = 0;
    bool overflow = false;
    std::array<bool,64> boltsSeen{};
};

void collectBolt(CarrierRoster& roster, const ActiveProcessView& actor) {
    const auto id = readMemory<std::uint32_t>(actor.handle.address + 0x10);
    const auto position = readMemory<totk::core::WorldPosition>(actor.handle.address + layout::kActorPosition);
    for (std::size_t i = 0; i < g_bolts.size(); ++i) {
        auto& bolt = g_bolts[i];
        if (roster.boltsSeen[i]) continue;
        const auto dx = position.x-bolt.position.x, dy = position.y-bolt.position.y, dz = position.z-bolt.position.z;
        const bool arrived = !bolt.id && bolt.waiting && dx*dx+dy*dy+dz*dz < 0.01f;
        if (bolt.id != id && !arrived) continue;
        if (arrived) {
            bolt.id = id;
            bolt.waiting = false;
            bolt.remaining = 1.0f;
        }
        roster.boltsSeen[i] = true;
        if (bolt.remaining <= 0) remove(actor.handle);
        return;
    }
    remove(actor.handle);
}

void collectCarrier(CarrierRoster& roster, const ActiveProcessView& actor) {
    if (actor.processState != 4) return;
    if (appearances::collect(actor)) return;
    if (actor.processName.equals(kEffectActors[static_cast<std::size_t>(Effect::UrbosaBolt)])) {
        collectBolt(roster,actor);
        return;
    }
    if (actor.processName.equals(kGaleUpdraftActor)) {
        if (roster.windCount < roster.winds.size()) {
            roster.winds[roster.windCount++] = actor.handle;
        } else {
            roster.overflow = true;
        }
        return;
    }
    for (std::size_t i = 0; i < kCarrierCount; ++i) {
        if (isWind(i) || !actor.processName.equals(name(i))) continue;
        if (!roster.actors[i].address) {
            roster.actors[i] = actor.handle;
        } else if (roster.duplicateCount < roster.duplicates.size()) {
            roster.duplicates[roster.duplicateCount++] = actor.handle;
        } else {
            roster.overflow = true;
        }
        break;
    }
}

bool collectCarriers(const SceneContext& scene, CarrierRoster& roster) {
    const auto access = resolveActiveProcessRoster(g_base, scene.token);
    if (!access) {
        Logging.Log("[champions] EFFECT_ROSTER_REFUSED error=%u", unsigned(access.error));
        return false;
    }
    const auto walk = visitActiveProcesses(access.value, [&](const ActiveProcessView& actor) {
        collectCarrier(roster, actor);
        return VisitControl::Continue;
    });
    if (!walk || !walk.value.complete || roster.overflow) {
        Logging.Log("[champions] EFFECT_ROSTER_INCOMPLETE error=%u overflow=%u",
                    walk ? 0 : unsigned(walk.error), unsigned(roster.overflow));
        return false;
    }
    return true;
}

void reconcileUpdrafts(CarrierRoster& roster) {
    std::array<WindFieldOwner, kWindCount> owners{};
    std::array<WindFieldActor, kMaxWindActors> candidates{};
    for (std::size_t i = 0; i < kWindCount; ++i) {
        const auto slot = kWindFirst + i;
        const auto& state = g_slots[slot];
        owners[i] = {g_hasFixed[slot] && (state.pulse.active() || state.pending || state.observed || state.deleting),
                     g_windIds[i], g_fixed[slot].position};
    }
    for (std::size_t i = 0; i < roster.windCount; ++i)
        candidates[i] = {readMemory<std::uint32_t>(roster.winds[i].address + 0x10),
                         readMemory<totk::core::WorldPosition>(roster.winds[i].address + layout::kActorPosition)};
    const auto matching = matchWindFields(owners, {candidates.data(), roster.windCount});
    for (std::size_t i = 0; i < matching.size(); ++i)
        if (matching[i] >= 0) {
            const auto match = static_cast<std::size_t>(matching[i]);
            roster.actors[kWindFirst + i] = roster.winds[match];
            g_windIds[i] = candidates[match].actorId;
            roster.winds[match] = {};
        }
    for (std::size_t i = 0; i < roster.windCount; ++i)
        if (roster.winds[i].address) remove(roster.winds[i]);
}

void startSpiritEffects(Blessing blessing) {
    if (blessing == Blessing::Daruk) return;
    struct Timing { float duration, reveal, leaveAfter; };
    constexpr std::array<Timing,4> timings{{
        {120/30.0f,25/30.0f,95/30.0f}, {70/30.0f,25/30.0f,55/30.0f},
        {19/30.0f,3/30.0f,11/30.0f}, {30/30.0f,12.5f/30.0f,20/30.0f}}};
    const auto timing = timings[unsigned(blessing)];
    const auto first = unsigned(blessing) * 4;
    g_slots[first].pulse.start(timing.reveal);
    g_slots[first+1].pulse.start(1.2f,timing.leaveAfter);
    g_slots[first+2].pulse.start(timing.duration);
    g_slots[first+3].pulse.start(std::min(1.5f,timing.duration));
}

void serviceCarrier(std::size_t i, ActorHandle actor, const SceneContext& scene,
                    const totk::core::WorldTransform& placement, const TransformService& transforms, float seconds) {
    auto& slot = g_slots[i];
    const auto step = observeCarrier(slot, actor.address != 0);
    if (isWind(i) && !actor.address && !slot.pending) g_windIds[i - kWindFirst] = 0;
    if (step.timedOut) Logging.Log("[champions] EFFECT_SPAWN_TIMEOUT effect=%u", unsigned(i));
    switch (step.action) {
    case CarrierAction::Delete:
        if (i == static_cast<std::size_t>(Effect::UrbosaRange)) field::reset();
        remove(actor);
        break;
    case CarrierAction::Follow: {
        auto target = g_hasFixed[i] ? g_fixed[i] : placement;
        if (chestHeight(i)) target.position.y += 1.0f;
        const auto moved = transforms.force(actor, scene.token, target);
        if (moved != TransformError::None)
            Logging.Log("[champions] EFFECT_FOLLOW_REFUSED effect=%u error=%u", unsigned(i), unsigned(moved));
        if (i == static_cast<std::size_t>(Effect::UrbosaRange))
            field::update(actor.address,target.position,seconds);
        break;
    }
    case CarrierAction::Spawn: {
        const auto target = g_hasFixed[i] ? g_fixed[i] : placement;
        if (spawn(i, target)) {
            slot.created();
            if (isWind(i)) g_windIds[i - kWindFirst] = 0;
        }
        break;
    }
    case CarrierAction::None:
        break;
    }
}
} // namespace

void reset() { appearances::reset(); field::reset(); g_slots = {}; g_hasFixed = {}; g_windIds = {}; g_bolts = {}; }

bool spawnAppearance(const char* actor,const totk::core::WorldTransform& placement) {
    return spawnActor(actor,placement,{1,1,1});
}

bool pulse(Effect effect, float seconds, float delay) {
    const auto index = static_cast<std::size_t>(effect);
    if (index >= kWindFirst) return false;
    g_hasFixed[index] = false;
    return g_slots[index].pulse.start(seconds, delay);
}

void hold(Effect effect, bool active) {
    const auto index = static_cast<std::size_t>(effect);
    if (index < kWindFirst) {
        if (active) g_hasFixed[index] = false;
        g_slots[index].pulse.hold(active);
    }
}

bool launchUpdraft(const totk::core::WorldTransform& placement) {
    if (!isFinite(placement)) return false;
    for (std::size_t index = kWindFirst; index < kCarrierCount; ++index) {
        auto& slot = g_slots[index];
        if (slot.pulse.active() || slot.pending || slot.observed || slot.deleting) continue;
        if (!slot.pulse.start(kGaleDurationSeconds)) return false;
        g_fixed[index] = placement;
        g_hasFixed[index] = true;
        if (!spawn(index, placement)) { slot = {}; return false; }
        g_windIds[index - kWindFirst] = 0;
        slot.created();
        pulse(Effect::RevaliUpdraft, kGaleDurationSeconds);
        const auto visual = static_cast<std::size_t>(Effect::RevaliUpdraft);
        g_fixed[visual] = placement;
        g_hasFixed[visual] = true;
        return true;
    }
    Logging.Log("[champions] GALE_FIELD_REFUSED reason=three-active-fields");
    return false;
}

bool updraftActive() {
    for (std::size_t index = kWindFirst; index < kCarrierCount; ++index)
        if (g_slots[index].pulse.active()) return true;
    return false;
}

bool launchStrike(const totk::core::WorldTransform& placement) {
    if (!isFinite(placement)) {
        Logging.Log("[champions] FURY_SPAWN_REFUSED reason=nonfinite-transform");
        return false;
    }
    const auto range = static_cast<std::size_t>(Effect::UrbosaRange);
    releaseFuryField(g_slots[range].pulse);
    g_fixed[range] = placement;
    g_hasFixed[range] = true;
    return true;
}

bool strikeEnemy(const totk::core::WorldTransform& placement) {
    if (!isFinite(placement)) {
        Logging.Log("[champions] FURY_BOLT_REFUSED reason=nonfinite-transform");
        return false;
    }
    for (auto& bolt : g_bolts) {
        if (bolt.id || bolt.waiting) continue;
        const auto actor = kEffectActors[static_cast<std::size_t>(Effect::UrbosaBolt)];
        if (!spawnActor(actor,placement,{1,1,1})) return false;
        bolt = {placement.position,0,5.0f,true};
        return true;
    }
    Logging.Log("[champions] FURY_BOLT_REFUSED reason=capacity");
    return false;
}

void used(Blessing blessing) {
    appearances::used(blessing);
    startSpiritEffects(blessing);
    if (blessing == Blessing::Daruk) {
        pulse(Effect::DarukShellBreak,1.0f);
        pulse(Effect::DarukFlare,1.5f);
        pulse(Effect::DarukImpact,1.0f);
        return;
    }
    if (blessing == Blessing::Mipha) {
        pulse(Effect::MiphaHeal, 6.8f);
        pulse(Effect::MiphaTrace, 6.8f);
        pulse(Effect::MiphaWater, 2.0f);
    }
}

void service(const SceneContext& scene, const ActorHandle& player, float seconds) {
    const auto transforms = TransformService(TransformFunctions::fromMainBase(g_base));
    const auto placement = transforms.read(player, scene.token);
    if (!placement) {
        Logging.Log("[champions] EFFECT_FRAME_REFUSED reason=player-transform error=%u", unsigned(placement.error));
        return;
    }
    for (std::size_t i = 0; i < g_slots.size(); ++i) {
        if (i == static_cast<std::size_t>(Effect::UrbosaRange) ||
            i == static_cast<std::size_t>(Effect::DarukShellBreak))
            g_slots[i].tickWhileVisible(seconds);
        else g_slots[i].tick(seconds);
    }
    for (auto& bolt : g_bolts) {
        bolt.remaining -= seconds;
        if (bolt.waiting && bolt.remaining <= 0) {
            Logging.Log("[champions] FURY_BOLT_TIMEOUT");
            bolt = {};
        }
    }
    CarrierRoster roster;
    appearances::beginFrame();
    if (!collectCarriers(scene, roster)) return;
    for (std::size_t i = 0; i < g_bolts.size(); ++i)
        if (g_bolts[i].id && !roster.boltsSeen[i]) g_bolts[i] = {};
    for (std::size_t i = 0; i < roster.duplicateCount; ++i) remove(roster.duplicates[i]);
    reconcileUpdrafts(roster);
    for (std::size_t i = 0; i < g_slots.size(); ++i)
        serviceCarrier(i, roster.actors[i], scene, placement.value, transforms, seconds);
    appearances::service(scene, placement.value, seconds);
}

bool install(std::uintptr_t base) {
    if (!codeMatches(base, kSites, "champions", "EFFECT_BUILD_REFUSED")) return false;
    if (!field::install(base)) return false;
    if (!appearances::install(base)) return false;
    g_base = base;
    return true;
}
} // namespace botw_abilities::champions::effects
