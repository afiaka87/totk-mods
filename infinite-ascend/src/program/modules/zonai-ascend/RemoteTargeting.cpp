#include "RemoteTargeting.hpp"

#include <cstddef>
#include <cstring>

namespace zonai_ascend::remote {
namespace {

Bindings g_native{};
struct Route {
    void* manager = nullptr;
    void* actor = nullptr;
    Vec3 origin{};
    Candidate candidate{};
    bool admitted = false;
};
Route g_aim{}, g_travel{};
bool g_arrivalChecked = false;

template<class T> T read(const void* base, std::ptrdiff_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const std::byte*>(base)+offset, sizeof(value));
    return value;
}
template<class T> void write(void* base, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(base)+offset, &value, sizeof(value));
}

bool find(void* manager, void* query, Vec3 origin) {
    if (!g_native.queryLayers || !g_native.blockedExit || !manager || !query)
        return false;
    Layers layers{};
    const Vec3 start{origin.x, origin.y+0.5f, origin.z};
    Candidate candidate{};
    const bool queried = finite(start) && g_native.queryLayers(&layers, &start);
    auto verdict = makeCandidate(queried, layers, origin, candidate);
    if (verdict == Verdict::Accepted && g_native.blockedExit(query, &candidate.exit))
        verdict = Verdict::BlockedExit;
    if (verdict != Verdict::Accepted) return false;
    g_aim = {manager, read<void*>(query, 0), origin, candidate, false};
    return g_aim.actor != nullptr;
}

void accept(Vec3 target) {
    g_aim.candidate.exit = target;
    g_aim.admitted = true;
}

}

void bind(Bindings bindings) {
    g_native = bindings;
    g_aim = {};
    g_travel = {};
    g_arrivalChecked = false;
}

void beginQuery(void* manager) {
    g_aim = {};
    if (manager && read<unsigned>(manager, 8) == 1) {
        g_travel = {};
        g_arrivalChecked = false;
    }
}

bool tryCeiling(void* manager, void* query, std::uint16_t reasons) {
    if (!g_native.queryLayers || !manager || read<unsigned>(manager, 8) != 1 || !query)
        return false;
    void* actor = read<void*>(query, 0);
    if (!actor) return false;
    if (!mayReplaceCeilingFailure(reasons)) return false;
    return find(manager, query, read<Vec3>(actor, 0x2B4));
}

void commit(void* manager) {
    g_travel = {};
    g_arrivalChecked = false;
    if (g_aim.admitted && manager == g_aim.manager &&
        near(read<Vec3>(manager, 0x24), g_aim.candidate.exit, 0.01f))
        g_travel = g_aim;
}

bool resolveFloor(void* manager, void* query, const Vec3* origin, Vec3* output,
                  bool* failed, bool nativePassed) {
    if (!manager || !query || !origin || !output || !g_native.queryLayers)
        return nativePassed;
    if (nativePassed) {
        if (g_aim.manager == manager && g_aim.actor == read<void*>(query, 0) &&
            finite(*output) && output->y > g_aim.candidate.entry.y)
            accept(*output);
        return true;
    }
    if (read<unsigned>(manager, 8) != 1) return false;
    const auto reasons = read<std::uint16_t>(manager, 0x7C);
    if (!mayReplaceFloorFailure(reasons)) {
        g_aim = {};
        return false;
    }
    // Query again rather than trusting the native scratch buffer after a failed/partial query.
    if (!find(manager, query, *origin)) { g_aim = {}; return false; }
    *output = g_aim.candidate.exit;
    if (failed) *failed = false;
    write<std::uint16_t>(manager, 0x7C, 0);
    if (g_native.isUnderground && g_native.isUnderground(query))
        write<std::uint8_t>(manager, 0x7F, read<std::uint8_t>(manager, 0x7F) | 4u);
    // The caller sets pending target and admission bits through its normal success path.
    accept(*output);
    return true;
}

const void* markerPosition(void* manager, void* actor, const void* original) {
    if (!g_aim.admitted || manager != g_aim.manager || actor != g_aim.actor ||
        !(read<std::uint8_t>(manager, 0x7F) & 1) ||
        !near(read<Vec3>(manager, 0x18), g_aim.candidate.exit, 0.01f) ||
        !near(read<Vec3>(actor, 0x2B4), g_aim.origin, 0.25f))
        return original;
    return &g_aim.candidate.entry;
}

void finishApproachCancel(void* selector) {
    if (!g_travel.admitted || !selector || !g_native.getActor || !g_native.getPlayer ||
        !g_native.getSelectorChild) return;
    // Native input handling has already recorded B; only accelerate the active approach child.
    const auto activeChild = read<std::uint8_t>(selector, 0x1E);
    if (!(read<std::uint8_t>(selector, 0x18) & 2) || activeChild == 0xFF ||
        activeChild != read<std::uint8_t>(selector, 0x70) ||
        !read<std::uint8_t>(selector, 0x5F)) return;
    void* actor = g_native.getActor(selector);
    if (!actor || actor != g_travel.actor) return;
    void* player = g_native.getPlayer(selector);
    void* child = g_native.getSelectorChild(selector);
    if (!player || !child ||
        !near(read<Vec3>(player, 0x132C), g_travel.candidate.exit, 0.01f)) return;
    const auto state = read<unsigned>(child, 0x20);
    const auto start = read<Vec3>(child, 0x24);
    if ((state != 1 && state != 2) || !finite(start) ||
        start.y >= g_travel.candidate.exit.y) return;
    const auto returnTarget = read<Vec3>(player, 0x1344);
    if (!near(returnTarget, g_travel.origin, 1.81f) || !near(start, returnTarget, 1.81f))
        return;
    const auto flags = read<std::uint32_t>(player, 0xAE4);
    if (flags & 0x80000u) return;
    // Native pre-cancel uses both bits; its return stage distinguishes this from an in-ceiling cancel.
    write<std::uint32_t>(player, 0xAE4, flags | 0x180000u);
    write<std::uint32_t>(player, 0xAEC, read<std::uint32_t>(player, 0xAEC) & ~0x10u);
    write<std::uint8_t>(selector, 0x5F, 0);
    g_travel = {};
}

bool cancelUnsafeArrival(void* node) {
    if (!g_travel.admitted || g_arrivalChecked || !node || !g_native.getActor ||
        !g_native.getPlayer || !g_native.snapFloor) return false;
    const auto state = read<unsigned>(node, 0x20);
    if (state < 2 || state > 5) return false;
    void* actor = g_native.getActor(node);
    const auto target = read<Vec3>(node, 0x3C);
    if (actor != g_travel.actor || !near(target, g_travel.candidate.exit, 1.81f) ||
        read<Vec3>(node, 0x24).y >= target.y)
        return false;
    Vec3 snapped{};
    g_arrivalChecked = true;
    if (g_native.snapFloor(actor, &target, &snapped) && near(snapped, target, 1.81f))
        return false;
    void* player = g_native.getPlayer(node);
    if (!player) { g_arrivalChecked = false; return false; }
    write<std::uint32_t>(player, 0xAE4, read<std::uint32_t>(player, 0xAE4) | 0x80000u);
    g_travel = {};
    return true;
}

}
