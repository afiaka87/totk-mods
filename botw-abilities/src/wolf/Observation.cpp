// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#if BOTW_ABILITIES_WOLF_DIAGNOSTIC
#include "Observation.hpp"
#include "NativeMotion.hpp"
#include "NativeNavigation.hpp"
#include "RenderObservation.hpp"
#include "ModelResources.hpp"
#include "support/SafeRead.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <atomic>
#include <lib.hpp>

namespace botw_abilities::wolf {
namespace {
std::array<std::atomic<std::uintptr_t>, 4> g_units;
std::atomic<std::uintptr_t> g_controller{};
std::atomic<unsigned> g_draws{}, g_triangles{}, g_requests{}, g_epoch{};
std::uint32_t g_generation = UINT32_MAX;
using support::safeRead;

template<class T> T read(std::uintptr_t address) {
    T value{};
    safeRead(address, &value, sizeof(value));
    return value;
}

HOOK_DEFINE_TRAMPOLINE(Draw) {
    static std::uintptr_t Callback(std::uintptr_t unit, void* context, int lod, std::uintptr_t flags) {
        const auto epoch = g_epoch.load(std::memory_order_acquire);
        bool watched = false;
        for (const auto& candidate : g_units) watched |= candidate.load(std::memory_order_relaxed) == unit;
        auto* triangles = reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(context) + 0x34);
        const auto before = watched ? *triangles : 0;
        const auto result = Orig(unit, context, lod, flags);
        if (watched && g_epoch.load(std::memory_order_acquire) == epoch) {
            g_draws.fetch_add(1, std::memory_order_relaxed);
            g_triangles.fetch_add(*triangles - before, std::memory_order_relaxed);
        }
        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(Play) {
    static bool Callback(std::uintptr_t controller, const char** command, int slot, int interpolation,
                         bool a, bool b, bool c, float blend, float extra) {
        const auto result = Orig(controller, command, slot, interpolation, a, b, c, blend, extra);
        if (controller == g_controller.load(std::memory_order_acquire) &&
            g_requests.fetch_add(1, std::memory_order_relaxed) < 16) {
            Logging.Log("[wolf-link] AS_REQUEST command=%s slot=%d accepted=%u",
                command && *command ? *command : "<null>", slot, unsigned(result));
        }
        return result;
    }
};
}

bool installObservation(std::uintptr_t base) {
    constexpr totk::engine::CodeSite sites[]{
        {0x74C284, 0xD10383FF, 0x6D0723E9}, {0xD280F4, 0xD10643FF, 0x6D1223E9},
    };
    if (!totk::engine::codeMatches(base, sites, "wolf-link", "OBSERVATION_ABI_REFUSED")) return false;
    Draw::InstallAtOffset(0x74C284);
    Play::InstallAtOffset(0xD280F4);
    return true;
}

void logObservation(const ActorReference& actor, std::uintptr_t base) {
    const auto components = read<std::uintptr_t>(actor.address() + 0x228);
    const auto component = read<std::uintptr_t>(components + 0x10);
    const auto root = read<std::uintptr_t>(component + 0x28);
    const auto count = read<int>(root + 0x20);
    const auto entries = read<std::uintptr_t>(root + 0x28);
    const auto model = count == 1 ? read<std::uintptr_t>(read<std::uintptr_t>(entries)) : 0;
    const auto skeleton = read<std::uintptr_t>(model + 0x170);
    const auto bones = read<std::uint16_t>(read<std::uintptr_t>(skeleton) + 0x38);
    const auto render = champions_poc::inspectRendering(safeRead, model);
    if (g_generation != actor.generation()) {
        g_epoch.fetch_add(1, std::memory_order_acq_rel);
        for (auto& unit : g_units) unit.store(0, std::memory_order_relaxed);
        g_draws.store(0, std::memory_order_relaxed);
        g_triangles.store(0, std::memory_order_relaxed);
        g_requests.store(0, std::memory_order_relaxed);
        g_generation = actor.generation();
    }
    for (unsigned i = 0; i < g_units.size(); ++i)
        g_units[i].store(i < render.shapes ? render.shape[i].unit : 0, std::memory_order_release);
    const auto pose = champions_poc::inspectSkeleton(safeRead, model);
    Logging.Log("[wolf-link] DRAW id=%u calls=%u triangles=%u pose=%u finite=%u singular=%u unknown=%u det=%.5g root=%.2f,%.2f,%.2f block=%zu",
        actor.generation(), g_draws.load(), g_triangles.load(), unsigned(pose.readable), pose.finite,
        pose.singular, pose.unreadable, pose.minimumDeterminant,
        double(pose.root[0]), double(pose.root[1]), double(pose.root[2]), std::size_t(pose.blockBytes));
    const auto textures = champions_poc::inspectTextures(safeRead, model);
    for (unsigned i = 0; i < textures.materials; ++i) {
        const auto& row = textures.material[i];
        Logging.Log("[wolf-link] TEXTURES id=%u material=%u readable=%u bound=%u slots=%u missing=%u unknown=%u",
            actor.generation(), i, unsigned(row.readable), row.bound, row.slots, row.missing, row.unreadable);
    }
    Logging.Log("[wolf-link] MODEL id=%u root=%p count=%d model=%p bones=%u render=%u shapes=%u units=%u visible=%u bounds=%u unknown=%u/%u/%u",
        actor.generation(), reinterpret_cast<void*>(root), count, reinterpret_cast<void*>(model), unsigned(bones),
        unsigned(render.readable), render.shapes, render.units, render.visible, render.validBounds,
        render.unreadable, render.unknownInputs, render.unknownBounds);
    for (unsigned i = 0; i < render.shapes && i < render.shape.size(); ++i) {
        const auto& shape = render.shape[i];
        Logging.Log("[wolf-link] SHAPE id=%u index=%u visible=%u inputs=%u/%u sphere=%.2f,%.2f,%.2f radius=%.2f",
            actor.generation(), i, unsigned(shape.visible), shape.boundInputs, shape.inputs,
            double(shape.sphere[0]), double(shape.sphere[1]), double(shape.sphere[2]), double(shape.sphere[4]));
    }
    const auto as = reinterpret_cast<std::uintptr_t(*)(std::uintptr_t)>(base + 0xFCA854)(actor.address());
    g_controller.store(as, std::memory_order_release);
    const auto quad = read<std::uintptr_t>(components + 0x3E0);
    Logging.Log("[wolf-link] AS_STATE id=%u slots=%d quadFlags=%x commandFlags=%x current=%u next=%u slot=%d",
        actor.generation(), read<int>(as + 24), read<unsigned>(quad + 544),
        read<unsigned char>(quad + 172), read<unsigned>(quad + 104),
        read<unsigned>(quad + 108), read<int>(quad + 116));
    std::array<char, 64> command{};
    if (as && read<int>(as + 24) > 0) {
        const auto name = reinterpret_cast<std::uintptr_t(*)(std::uintptr_t, int)>(base + 0xD2881C)(as, 0);
        safeRead(name, command.data(), command.size() - 1);
    }
    const auto ready = localFollowingReady(actor);
    const auto motion = readMotion(actor);
    Logging.Log("[wolf-link] MOTION id=%u as=%p command=%s ready=%d gear=%d pending=%d rate=%.3f",
        actor.generation(), reinterpret_cast<void*>(as), command.data(), ready ? int(*ready) : -1,
        motion ? motion->gears.gear : -1, motion ? motion->gears.pendingGear : -1,
        motion ? double(motion->playRate) : -1.0);
}
}
#endif
