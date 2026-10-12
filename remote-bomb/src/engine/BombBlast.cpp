// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombBlast.hpp"
#include "totk/engine/CodeCheck.hpp"
#include "BombIdentity.hpp"
#include "BombPressure.hpp"
#include "BlastWindow.hpp"
#include "BlastDamage.hpp"
#include "BlastLifetime.hpp"
#include "NativeBlastContact.hpp"
#include "NativeBlastDamage.hpp"
#include "NativeBlastLifetime.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Totk121Offsets.hpp"
#include <lib.hpp>
#include <atomic>
#include <cmath>
#include <cstring>

namespace remote_bomb::engine::explosion {
namespace eng = totk::engine;
namespace {
using blast::Vector;
std::uintptr_t mainBase{};
std::atomic_flag lock = ATOMIC_FLAG_INIT;
blast::Windows<> windows;
blast::DamageSources<> damageSources;
blast::Lifetimes<> lifetimes;
std::uintptr_t player{};
unsigned scene{};
std::uint64_t clock{};
bool running{};
constexpr totk::engine::CodeSite guards[]{
    {0x1396A0C, 0xD10543FF, 0xFD007BEA}, {0x13884B0, 0xD10203FF, 0xA9057BFD},
    {0x138B024, 0xA9BE7BFD, 0xF9000BF3},
    {0x138B0D0, 0xD10103FF, 0x6D0123E9},
    {0x1387D1C, 0xA9BF7BFD, 0x910003FD}, {0x2698930, 0xBD404800, 0xD65F03C0},
    {0x2698754, 0x91013000, 0xD65F03C0}, {0x17003CC, 0xD101C3FF, 0xA9047BFD},
    {0xB530B0, 0xFC1B0FEA, 0x6D00A3E9}, {0x6AB438, 0xF9403008, 0xB4000068},
    {0x16FEB00, 0xD10383FF, 0xA9097BFD},
    {0x13937BC, 0x6DB923E9, 0xA9017BFD},
    {0x138FE84, 0xA9BE7BFD, 0xA9014FF4},
    {0x15A935C, 0xD10243FF, 0x6D0423E9},
};
struct Locked {
    Locked() { while (lock.test_and_set(std::memory_order_acquire)) {} }
    ~Locked() { lock.clear(std::memory_order_release); }
};
template <class T> T read(std::uintptr_t address) { return eng::readMemory<T>(address); }
template <class F> F function(std::uintptr_t offset) { return reinterpret_cast<F>(mainBase + offset); }
bool valid(std::uintptr_t address) { return eng::isPlausibleAddress(address); }
bool finite(Vector value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
bool nonzero(Vector value) { return value.x != 0.f || value.y != 0.f || value.z != 0.f; }
const char *actorName(std::uintptr_t actor) {
    const auto name = valid(actor) ? read<std::uintptr_t>(actor + eng::layout::kActorNamePointer) : 0;
    return eng::isPlausibleStringAddress(name) ? reinterpret_cast<const char *>(name) : nullptr;
}
bool privateActor(std::uintptr_t actor) {
    const auto name = actorName(actor);
    return name && identifyBomb(name).has_value();
}
bool privateBlast(std::uintptr_t element) {
    if (!valid(element) || read<std::uintptr_t>(element) != mainBase + 0x42DDFD0 ||
        function<unsigned (*)(std::uintptr_t)>(0x1387D1C)(element) != 14)
        return false;
    const auto object = read<std::uintptr_t>(element + 96);
    const auto actor = valid(object) ? read<std::uintptr_t>(object + 624) : 0;
    return privateActor(actor);
}
void refused(std::uintptr_t source, std::uintptr_t target, const char *reason, std::uintptr_t value = 0) {
    Logging.Log("[remote-bomb] BLAST_REFUSED source=%p target=%p reason=%s value=%p",
                reinterpret_cast<void *>(source), reinterpret_cast<void *>(target), reason,
                reinterpret_cast<void *>(value));
}
struct Target {
    std::uintptr_t body{};
    Vector position{};
    float volume{};
    bool isPlayer{};
};
// A loose dynamic body the blast may push; Link is reported separately and never pushed.
Target targetBody(std::uintptr_t object) {
    const auto bodies = valid(object) ? read<std::uintptr_t>(object + 24) : 0;
    if (!valid(bodies) || read<std::uintptr_t>(bodies) != mainBase + 0x42DED08)
        return {};
    const auto actor = read<std::uintptr_t>(bodies + 24);
    {
        Locked guard;
        if (actor && actor == player)
            return {.isPlayer = true};
    }
    const auto registry = valid(actor) ? read<std::uintptr_t>(actor + eng::layout::kActorComponentRegistry) : 0;
    const auto physics = valid(registry) ? read<std::uintptr_t>(registry + eng::layout::kPhysicsFromRegistry) : 0;
    const auto controller = valid(physics) ? read<std::uintptr_t>(physics + eng::layout::kRigidBodySetFromPhysics) : 0;
    const auto body = valid(controller) ? read<std::uintptr_t>(controller + eng::layout::kRigidBodyFromSet) : 0;
    if (!valid(body) || function<unsigned (*)(std::uintptr_t)>(0x6AB438)(body) != 2 ||
        (read<std::uint64_t>(body + 104) & 0x400000))
        return {};
    const auto point = function<const Vector *(*)(std::uintptr_t)>(0x2698754)(bodies);
    const auto volume = function<float (*)(std::uintptr_t)>(0x2698930)(bodies);
    if (!point || !finite(*point) || !std::isfinite(volume) || volume <= 0.f)
        return {};
    return {body, *point, volume};
}
}
bool initialize(std::uintptr_t base) {
    mainBase = 0;
    if (!base) {
        refused(0, 0, "base");
        return false;
    }
    if (!totk::engine::codeMatches(base, guards, "remote-bomb", "BLAST_BIND_REFUSED"))
        return false;
    mainBase = base;
    return true;
}
void sceneChanged(unsigned generation) {
    Locked guard;
    if (scene != generation) {
        windows = {};
        damageSources = {};
        lifetimes = {};
        player = 0;
        scene = generation;
    }
}
void publishTime(std::uint64_t value, bool gameplay) {
    Locked guard;
    clock = value;
    running = gameplay;
}
void publishPlayer(std::uintptr_t actor) {
    Locked guard;
    player = actor;
}
void elementPhysicsPrepared(std::uintptr_t element, std::uintptr_t simulation) {
    if (!privateBlast(element))
        return;
    const auto flags = read<unsigned>(element + 8);
    if ((flags & 7) != 3) {
        { Locked guard; lifetimes.observe(element, false); }
        refused(element, 0, "phase-inactive", flags);
        return;
    }
    const auto count = read<int>(element + 80);
    const auto instances = read<std::uintptr_t>(element + 88);
    const auto instance = count == 1 && valid(instances) ? read<std::uintptr_t>(instances) : 0;
    const auto object = read<std::uintptr_t>(element + 96);
    const auto actor = valid(object) ? read<std::uintptr_t>(object + 624) : 0;
    if (!valid(instance) || !valid(object) || !valid(actor) || !valid(simulation)) {
        { Locked guard; lifetimes.observe(element, false); }
        refused(element, object, "damage-source-instance", instance);
        return;
    }
    const auto origin = read<Vector>(instance + 128);
    const auto nativeAge = read<float>(element + 32);
    const auto frameDelta = read<float>(simulation + 8);
    blast::DamageSync damageSync = blast::DamageSync::Invalid;
    std::optional<blast::DamageSources<>::Source> damage;
    blast::Activation activation = blast::Activation::Unavailable;
    unsigned context{};
    std::uint64_t startedAt{};
    {
        Locked guard;
        context = scene;
        startedAt = clock;
        if (running && scene) {
            activation = windows.activate(element, clock);
            damageSync = damageSources.synchronize(element, object, actor, origin, nativeAge, frameDelta);
            damage = damageSources.find(object, actor);
            const bool coherent = damageSync != blast::DamageSync::Invalid &&
                                  damageSync != blast::DamageSync::IdentityChanged &&
                                  damageSync != blast::DamageSync::Capacity;
            lifetimes.observe(element, coherent && damage && damage->sensor.active);
        }
    }
    if (activation == blast::Activation::Started)
        Logging.Log("[remote-bomb] BLAST_ACTIVE source=%p scene=%u clock=%llu",
                    reinterpret_cast<void *>(element), context, static_cast<unsigned long long>(startedAt));
    else if (activation != blast::Activation::Existing)
        refused(element, 0, "activation-window", context ? unsigned(activation) : 0);
    if (damage && (damageSync == blast::DamageSync::Started || damageSync == blast::DamageSync::Advanced ||
                   damageSync == blast::DamageSync::Expired))
        Logging.Log("[remote-bomb] BLAST_DAMAGE_STEP source=%p age=%f radius=%f remaining=%f active=%u",
                    reinterpret_cast<void *>(element), double(nativeAge), double(damage->sensor.radius),
                    double(damage->sensor.remaining), unsigned(damage->sensor.active));
    else if (damageSync != blast::DamageSync::Unchanged)
        Logging.Log("[remote-bomb] BLAST_DAMAGE_REFUSED source=%p age=%f status=%u scene=%u",
                    reinterpret_cast<void *>(element), double(nativeAge), unsigned(damageSync), context);
}
// Decides which targets the growing blast reaches; only these contacts go on to native damage.
static bool admitDamageContact(DamageContact contact, bool native) {
    const auto bodies = valid(contact.target) ? read<std::uintptr_t>(contact.target + 24) : 0;
    const bool typed = valid(bodies) && read<std::uintptr_t>(bodies) == mainBase + 0x42DED08;
    const auto name = actorName(typed ? read<std::uintptr_t>(bodies + 24) : 0);
    std::optional<blast::DamageSources<>::Source> source;
    bool gameplay{};
    {
        Locked guard;
        gameplay = running && scene;
        source = damageSources.find(contact.source, contact.actor);
    }
    if (!gameplay || !source || !source->sensor.active) {
        Logging.Log("[remote-bomb] BLAST_DAMAGE_BLOCKED actor=%p source=%p target=%p name=%.48s native=%u gameplay=%u tracked=%u active=%u",
                    reinterpret_cast<void *>(contact.actor), reinterpret_cast<void *>(contact.source),
                    reinterpret_cast<void *>(contact.target), name ? name : "?", unsigned(native),
                    unsigned(gameplay), unsigned(source.has_value()), unsigned(source && source->sensor.active));
        return false;
    }
    if (!typed) {
        refused(source->element, contact.target, "damage-target-type", bodies);
        return false;
    }
    Vector point{};
    const auto hit = function<bool (*)(std::uintptr_t, Vector *, const Vector *, float)>(0x13937BC)(
        bodies, &point, &source->origin, source->sensor.radius);
    if (!hit || !finite(point)) {
        Logging.Log("[remote-bomb] BLAST_DAMAGE_OUTSIDE source=%p target=%p name=%.48s native=%u radius=%f hit=%u point=%f,%f,%f",
                    reinterpret_cast<void *>(source->element), reinterpret_cast<void *>(contact.target),
                    name ? name : "?", unsigned(native), double(source->sensor.radius), unsigned(hit),
                    double(point.x), double(point.y), double(point.z));
        return false;
    }
    Logging.Log("[remote-bomb] BLAST_DAMAGE_ADMITTED source=%p target=%p name=%.48s native=%u radius=%f age=%f",
                reinterpret_cast<void *>(source->element), reinterpret_cast<void *>(contact.target),
                name ? name : "?", unsigned(native), double(source->sensor.radius), double(source->nativeAge));
    return true;
}
// Native damage for an admitted contact is calculated a few frames after the blast window closes, and its target is a chemistry object rather than a body set, so it is reported but never gated.
void reportChemicalDamage(std::uintptr_t info) {
    const auto contact = chemicalDamageContact(info, []<class T>(std::uintptr_t address) { return read<T>(address); }, valid);
    if (!privateActor(contact.actor))
        return;
    const auto target = valid(contact.target) ? read<std::uintptr_t>(contact.target + 624) : 0;
    const auto name = actorName(target);
    Logging.Log("[remote-bomb] BLAST_NATIVE_DAMAGE actor=%p source=%p target=%p name=%.48s",
                reinterpret_cast<void *>(contact.actor), reinterpret_cast<void *>(contact.source),
                reinterpret_cast<void *>(target), name ? name : "?");
}
bool admitElementListClear(std::uintptr_t list) {
    std::array<blast::Lifetimes<>::Source, 8> sources;
    { Locked guard; sources = lifetimes.entries; }
    for (const auto &source : sources) {
        if (!source.retained() || !privateActor(source.actor) || !valid(source.object) ||
            read<std::uintptr_t>(source.object + 624) != source.actor)
            continue;
        const auto ownedList = explosionElementList(source.object,
            []<class T>(std::uintptr_t address) { return read<T>(address); }, valid);
        if (!list || list != ownedList)
            continue;
        if (!calculatingBombOwner(source.actor,
                []<class T>(std::uintptr_t address) { return read<T>(address); }, valid)) {
            Logging.Log("[remote-bomb] BLAST_CLEANUP_NATIVE actor=%p source=%p state=%u phase=%u",
                        reinterpret_cast<void *>(source.actor), reinterpret_cast<void *>(source.element),
                        read<unsigned>(source.actor + 36), unsigned(source.phase));
            continue;
        }
        Logging.Log("[remote-bomb] BLAST_CLEAR_DEFERRED source=%p actor=%p list=%p phase=%u count=%u",
                    reinterpret_cast<void *>(source.element), reinterpret_cast<void *>(source.actor),
                    reinterpret_cast<void *>(list), unsigned(source.phase), read<unsigned>(source.object + 704));
        return false;
    }
    return true;
}
bool admitDeathProcedure(std::uintptr_t component) {
    const auto actor = deathProcedureActor(component,
        []<class T>(std::uintptr_t address) { return read<T>(address); }, valid);
    if (!privateActor(actor))
        return true;
    std::optional<blast::Lifetimes<>::Source> source;
    { Locked guard; source = lifetimes.findActor(actor); }
    if (!source)
        return true;
    if (!calculatingBombOwner(actor,
            []<class T>(std::uintptr_t address) { return read<T>(address); }, valid)) {
        Logging.Log("[remote-bomb] BLAST_DEATH_NATIVE actor=%p source=%p state=%u phase=%u",
                    reinterpret_cast<void *>(actor), reinterpret_cast<void *>(source->element),
                    read<unsigned>(actor + 36), unsigned(source->phase));
        return true;
    }
    Logging.Log("[remote-bomb] BLAST_DEATH_DEFERRED source=%p actor=%p component=%p phase=%u state=%u",
                reinterpret_cast<void *>(source->element), reinterpret_cast<void *>(actor),
                reinterpret_cast<void *>(component), unsigned(source->phase), read<unsigned>(component + 32));
    return false;
}
void instanceCreated(std::uintptr_t instance) {
    const auto element = valid(instance) ? read<std::uintptr_t>(instance + 24) : 0;
    if (privateBlast(element)) {
        const auto object = read<std::uintptr_t>(element + 96);
        const auto actor = read<std::uintptr_t>(object + 624);
        Locked guard;
        windows.reset(element);
        damageSources.remove(element);
        const auto status = scene ? lifetimes.arm(element, object, actor) : blast::LifetimeArm::Invalid;
        Logging.Log("[remote-bomb] BLAST_LIFETIME_ARMED source=%p actor=%p scene=%u status=%u",
                    reinterpret_cast<void *>(element), reinterpret_cast<void *>(actor), scene, unsigned(status));
    }
}
void instanceRemoved(std::uintptr_t instance) {
    const auto element = valid(instance) ? read<std::uintptr_t>(instance + 24) : 0;
    Locked guard;
    windows.reset(element);
    const auto removed = damageSources.remove(element);
    lifetimes.remove(element);
    if (removed)
        Logging.Log("[remote-bomb] BLAST_DAMAGE_REMOVED source=%p age=%f remaining=%f active=%u",
                    reinterpret_cast<void *>(element), double(removed->nativeAge),
                    double(removed->sensor.remaining), unsigned(removed->sensor.active));
}
void calculateContact(ContactCalculation original, std::uintptr_t contact,
                      std::uintptr_t calculation, void *output) {
    const auto interface = valid(calculation) ? read<std::uintptr_t>(calculation + 16) : 0;
    const auto element = valid(interface) && read<std::uintptr_t>(interface) == mainBase + 0x42DE230 ? interface - 64 : 0;
    if (!privateBlast(element)) {
        original(contact, calculation, output);
        return;
    }
    const auto prior = captureForces(reinterpret_cast<const void *>(contact));
    alignas(8) unsigned char result[32]{};
    original(contact, calculation, result);
    const auto object = read<std::uintptr_t>(contact);
    const auto source = read<std::uintptr_t>(element + 96);
    const auto actor = valid(source) ? read<std::uintptr_t>(source + 624) : 0;
    setShockDamageFlag(result, admitDamageContact({actor, source, object}, shockDamageFlag(result)));
    copyShockResult(output, result);
    // Link keeps the game's own blast reaction and damage; the mod pushes only loose objects.
    const auto target = targetBody(object);
    if (target.isPlayer)
        return;
    if (!target.body) {
        refused(element, object, "target-body-or-volume");
        return;
    }
    const auto count = read<int>(element + 80);
    const auto instances = read<std::uintptr_t>(element + 88);
    if (count != 1 || !valid(instances)) {
        refused(element, object, "instance-count", unsigned(count));
        return;
    }
    const auto instance = read<std::uintptr_t>(instances);
    if (!valid(instance)) {
        refused(element, object, "instance", instance);
        return;
    }
    const auto origin = read<Vector>(instance + 128);
    if (!finite(origin)) {
        refused(element, object, "origin", instance);
        return;
    }
    blast::Admission admission = blast::Admission::Unavailable;
    unsigned context{};
    {
        Locked guard;
        context = scene;
        if (running && scene)
            admission = windows.admit(element, object, clock);
    }
    if (admission == blast::Admission::Capacity || admission == blast::Admission::Unavailable) {
        refused(element, object, "force-window", context ? unsigned(admission) : 0);
        return;
    }
    Vector nativePressure;
    std::memcpy(&nativePressure, result, sizeof(nativePressure));
    const auto geometry = blast::targetGeometry(target.volume);
    const auto pressure = blast::pressureWave({target.position.x - origin.x, target.position.y - origin.y,
                                               target.position.z - origin.z}, geometry.radius, 4.f,
                                              !nonzero(nativePressure));
    const auto impulse = blast::pressureImpulse(blast::incidentVelocity(pressure), geometry.projectedArea,
                                               1.f, 1.f, true);
    if (!finite(impulse)) {
        refused(element, object, "nonfinite-impulse", target.body);
        return;
    }
    if (admission != blast::Admission::Apply || !nonzero(impulse)) {
        restoreForces(reinterpret_cast<void *>(contact), prior);
        return;
    }
    Vector center;
    function<void (*)(std::uintptr_t, Vector *)>(0x17003CC)(target.body, &center);
    if (!finite(center)) {
        refused(element, object, "center", target.body);
        return;
    }
    function<std::uintptr_t (*)(std::uintptr_t, const Vector *, const Vector *)>(0xB530B0)(target.body, &impulse, &center);
    restoreForces(reinterpret_cast<void *>(contact), prior);
    Logging.Log("[remote-bomb] BLAST_IMPULSE source=%p target=%p body=%p volume=%f impulse=%f,%f,%f",
                reinterpret_cast<void *>(element), reinterpret_cast<void *>(object),
                reinterpret_cast<void *>(target.body), double(target.volume), double(impulse.x),
                double(impulse.y), double(impulse.z));
}
}
