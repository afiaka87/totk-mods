// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "GaleRender.hpp"
#include "GaleModels.hpp"
#include "GaleNative.hpp"
#include "GalePoseStorage.hpp"
#include "GaleResources.hpp"
#include "GalePoseTime.hpp"
#include "Runtime.hpp"
#include "totk/engine/ActorRoster.hpp"
#include "totk/port/CodeCheck.hpp"
#include "totk/render/PoseInput.hpp"
#include <atomic>
#include <bit>
#include <lib.hpp>

namespace botw_abilities::champions::pose {
namespace {
using namespace models;
using namespace totk::engine;
using namespace totk::port::literals;
constexpr totk::port::Site kSites[]{
    {0x76B6EC,0xD10283FF,0xA9047BFD}, {0x76C078,0xD103C3FF,0xA9097BFD},
    {0x824A8,0xA9BD7BFD,0xA90157F6}, {0x74F450,0xA9BC7BFD,0xA9015FF8},
    {0x756E98,0xD10283FF,0xA9047BFD}, {0x753530,0xA9BB7BFD,0xF9000BF9},
    {0x86A288,0xA9BD7BFD,0xA90157F6}, {0x6333C8,0xD100C3FF,0xFD000BE8},
    {0x833618,0xA9BA7BFD,0xA9016FFC},
};
// 1.4.3 inlines BeforeDraw and CalculateBounds, so function hooks miss them; the resume points have X19 = unit.
constexpr std::ptrdiff_t kDrawResume143 = 0x7DB70_v143, kBoundsResume143 = 0x7CB38_v143;
constexpr CodeSite kResumeSites143[]{{kDrawResume143, 0x39405268, 0x52800089},
                                     {kBoundsResume143, 0xF9402274, 0xB4FEBD54}};
constexpr totk::port::Member kBufferIndex{0x15, 0x14};
std::uintptr_t g_base{};
bool g_installed = false;
constexpr unsigned kPublishedLimit = 128;
std::array<std::atomic<std::uintptr_t>, kPublishedLimit> g_models{};
std::atomic<unsigned> g_count{};
std::atomic<std::uintptr_t> g_controller{};
std::atomic<std::uint64_t> g_scene{}, g_time{}, g_refusals{};
std::atomic<std::uint32_t> g_rendered{};
std::atomic_flag g_timeLock = ATOMIC_FLAG_INIT;
PoseTime g_poseTime;

enum class Refusal : unsigned { Capacity, Workspace, Player, Owner, Model, Body, Clock, Binding, Transform, Input };
void refused(Refusal code, unsigned detail = 0, const void* actor = nullptr) {
    const auto channel = gale::channelEpoch();
    auto previous = g_refusals.load(std::memory_order_relaxed);
    for (;;) {
        const auto mask = std::uint32_t(previous >> 32) == channel ? std::uint32_t(previous) : 0;
        const auto bit = 1u << unsigned(code);
        if (mask & bit) return;
        const auto next = (std::uint64_t(channel) << 32) | mask | bit;
        if (g_refusals.compare_exchange_weak(previous, next, std::memory_order_relaxed)) break;
    }
    std::string_view label;
    const auto* name = read<const char*>(actor, layout::kActorNamePointer);
    if (name && admit(name, 0, 128))
        if (const auto* end = static_cast<const char*>(std::memchr(name, 0, 128))) label = {name, end};
    Logging.Log("[champions] GALE_POSE_REFUSED reason=%u detail=%u channel=%u actor=%.*s",
        unsigned(code), detail, channel, int(label.size()), label.empty() ? "" : label.data());
}

// A posed copy of one model unit for the bounds routine; it copies only the running game's unit size.
struct BoundsInput {
    alignas(16) std::byte unit[kUnitCopyLimit];
    void prepare(const void* live, const totk::render::PoseInput& animation) {
        std::memcpy(unit, live, kLayout.size);
        const void* skeleton = animation.skeleton;
        std::memcpy(unit + kLayout.skeleton, &skeleton, sizeof(skeleton));
        const std::array<float, 3> zero{};
        std::memcpy(unit + kLayout.origin, zero.data(), sizeof(zero));
        unit[std::ptrdiff_t(kLayout.relativeFlag)] &= std::byte{0xFE};
    }
};

struct Workspace {
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    std::array<LocalPose, 128> local, morph;
    std::array<PoseMatrix, 128> oldBody, newBody;
    std::array<bool, 128> found{};
    std::array<PoseMatrix, kModelBoneLimit> output;
    totk::render::PoseInput input;
    BoundsInput bounds;
    View target;
    std::uint64_t tick = UINT64_MAX, scene = 0, time = 0;
    std::uint32_t channel = 0, playerId = 0;
};
// Observed peak is three concurrent poses; a fourth keeps one in reserve.
std::array<Workspace, 4> g_workspaces;
std::atomic<unsigned> g_leased{}, g_peakLeased{};
struct Lease {
    Workspace* work = nullptr;
    Owner owner;
    Lease() {
        for (auto& candidate : g_workspaces)
            if (!candidate.busy.test_and_set(std::memory_order_acquire)) { work = &candidate; break; }
        if (!work) return;
        const auto leased = g_leased.fetch_add(1, std::memory_order_relaxed) + 1;
        auto peak = g_peakLeased.load(std::memory_order_relaxed);
        while (leased > peak && !g_peakLeased.compare_exchange_weak(peak, leased, std::memory_order_relaxed)) {}
        if (leased > peak)
            Logging.Log("[champions] GALE_POSE_WORKSPACES peak=%u of %u", leased, unsigned(g_workspaces.size()));
    }
    ~Lease() {
        if (!work) return;
        g_leased.fetch_sub(1, std::memory_order_relaxed);
        work->busy.clear(std::memory_order_release);
    }
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
};

bool hinted(const void* unit) {
    if (!gale::active()) return false;
    const auto count = std::min(g_count.load(std::memory_order_acquire), kPublishedLimit);
    for (unsigned i = 0; i < count; ++i)
        if (g_models[i].load(std::memory_order_relaxed) == reinterpret_cast<std::uintptr_t>(unit)) return true;
    return false;
}

bool canonical(const void* player, Workspace& work, const GalePose& asset, float frames) {
    work.found.fill(false);
    const bool valid = visitModels(player, [&](const void* unit) {
        View view;
        if (!describe(g_base, unit, view)) return true;
        for (unsigned bone = 0; bone < view.identity.boneCount; ++bone) {
            const auto index = poseBone(asset, boneName(view, bone));
            if (index >= 0 && !work.found[unsigned(index)])
                work.found[unsigned(index)] = worldMatrix(view, bone, work.oldBody[unsigned(index)]);
        }
        return true;
    });
    if (!valid || !work.found[0]) return false;
    return asset.sampleChannel(frames, asset.continuation(), work.local, work.morph) &&
        buildPoseHierarchy(std::span(work.local).first(asset.boneCount()),
        [&](unsigned bone) { return asset.parent(bone); }, work.newBody) &&
        anchorPose(std::span(work.newBody).first(asset.boneCount()), work.oldBody[0]);
}

bool bodyBone(const View& view, unsigned bone, Workspace& work, const GalePose& asset, PoseMatrix& out) {
    PoseMatrix current;
    if (!worldMatrix(view, bone, current)) return false;
    unsigned anchor = bone;
    for (unsigned step = 0; step < view.identity.boneCount; ++step) {
        const int index = poseBone(asset, boneName(view, anchor));
        if (index >= 0 && work.found[unsigned(index)])
            return followPoseParent(current, work.oldBody[unsigned(index)], work.newBody[unsigned(index)], out);
        const int ancestor = parent(view, anchor);
        if (ancestor < 0 || unsigned(ancestor) >= view.identity.boneCount || unsigned(ancestor) == anchor) return false;
        anchor = unsigned(ancestor);
    }
    return false;
}

std::string_view targetName(const void* bind) {
    const auto* text = read<const char*>(bind, 0x88);
    if (!text || read<char>(text, 0) == 0) return "Root";
    if (!admit(text, 0, 255)) return {};
    const auto* end = static_cast<const char*>(std::memchr(text, 0, 255));
    return end ? std::string_view(text, end) : std::string_view{};
}

bool posedBone(const void* player, const void* actor, const View& view, unsigned bone,
                Workspace& work, const GalePose& asset, unsigned depth, PoseMatrix& out) {
    if (depth >= 8) return false;
    if (actor == player) return bodyBone(view, bone, work, asset, out);
    const auto* components = registry(actor);
    const auto* same = read<const void*>(components, 0x420);
    const auto* bind = same ? same : read<const void*>(components, 0x18);
    if (!bind || !admit(bind, 0x70, 0x18)) return false;
    ActorReference reference(g_base, at(bind, 0x70));
    const auto* parentActor = reference.get();
    if (!parentActor || parentActor == actor) return false;
    if (same) return parentActor == player && bodyBone(view, bone, work, asset, out);
    const auto name = targetName(bind);
    View parentView;
    unsigned parentIndex{};
    if (name.empty() || !findBone(g_base, parentActor, name, parentView, parentIndex)) return false;
    PoseMatrix current, oldParent, nextParent;
    return worldMatrix(view, bone, current) && worldMatrix(parentView, parentIndex, oldParent) &&
        posedBone(player, parentActor, parentView, parentIndex, work, asset, depth + 1, nextParent) &&
        followPoseParent(current, oldParent, nextParent, out);
}

// Link's crouched body is sampled once per tick, scene, channel, clock and player.
bool refreshBody(const void* player, Workspace& work, const GalePose& animation, float frames,
                 std::uint64_t scene, std::uint32_t channel, std::uint64_t time) {
    const auto tick = simulationTick();
    const auto playerId = read<std::uint32_t>(player, 0x10);
    if (work.tick == tick && work.scene == scene && work.channel == channel && work.time == time &&
        work.playerId == playerId) return true;
    work.tick = UINT64_MAX;
    if (!canonical(player, work, animation, frames)) { refused(Refusal::Body); return false; }
    work.scene = scene; work.channel = channel; work.time = time;
    work.playerId = playerId; work.tick = tick;
    return true;
}

bool poseTarget(const void* player, const void* owner, Workspace& work, const GalePose& animation) {
    const bool body = owner == player || read<const void*>(registry(owner), 0x420);
    PoseMatrix gearDelta;
    if (!body) {
        PoseMatrix original, posed, inverse;
        if (!worldMatrix(work.target, 0, original) ||
            !posedBone(player, owner, work.target, 0, work, animation, 0, posed) ||
            !inversePose(original, inverse) || !composePose(posed, inverse, gearDelta)) {
            refused(Refusal::Binding, 0, owner); return false;
        }
    }
    for (unsigned bone = 0; bone < work.target.identity.boneCount; ++bone) {
        PoseMatrix current;
        const bool posed = body ? posedBone(player, owner, work.target, bone, work, animation, 0, work.output[bone])
            : worldMatrix(work.target, bone, current) && composePose(gearDelta, current, work.output[bone]);
        if (!posed) {
            refused(Refusal::Binding, bone, owner); return false;
        }
        if (work.target.relative && !rebasePoseOrigin(work.output[bone], {}, work.target.origin)) {
            refused(Refusal::Transform, bone); return false;
        }
    }
    return true;
}

bool prepare(const void* unit, Lease& lease) {
    const auto* animation = asset();
    if (!animation || !currentContext(Blessing::Revali).gameplay) return false;
    if (!lease.work) { refused(Refusal::Workspace); return false; }
    const auto scene = resolveScene(g_base);
    if (!scene || scene.value.token.value != g_scene.load(std::memory_order_acquire)) return false;
    const auto players = resolvePlayerActors(scene.value);
    if (!players) { refused(Refusal::Player); return false; }
    const auto* player = reinterpret_cast<const void*>(players.value.player.address);
    if (!findOwner(g_base, player, unit, lease.owner)) { refused(Refusal::Owner); return false; }
    auto& work = *lease.work;
    if (!describe(g_base, unit, work.target)) { refused(Refusal::Model, boneCountOf(unit), lease.owner.actor); return false; }
    const auto time = g_time.load(std::memory_order_acquire);
    const auto channel = gale::channelEpoch();
    const float frames = channelPoseFrames(std::uint32_t(time >> 32) == channel ? std::bit_cast<float>(std::uint32_t(time)) : 0,
        animation->duration(0), kGaleChannelSeconds * 30.0f);
    if (!refreshBody(player, work, *animation, frames, scene.value.token.value, channel, time) ||
        !poseTarget(player, lease.owner.actor, work, *animation)) return false;
    const auto status = work.input.prepare(work.target.identity, work.target.identity, true,
        work.output.data(), kLayout, admit);
    if (status != totk::render::PoseInputStatus::Ready) { refused(Refusal::Input, unsigned(status), lease.owner.actor); return false; }
    if (!gale::active() || channel != gale::channelEpoch() || !currentContext(Blessing::Revali).gameplay) return false;
    if (g_rendered.exchange(channel, std::memory_order_relaxed) != channel)
        Logging.Log("[champions] GALE_POSE_PREPARED channel=%u frames=%f bones=%u", channel, double(frames), work.target.identity.boneCount);
    return true;
}

void observeAnimation(const void* controller, float frames) {
    if (reinterpret_cast<std::uintptr_t>(controller) != g_controller.load(std::memory_order_acquire) ||
        !gale::active() || !currentContext(Blessing::Revali).gameplay) return;
    const auto scene = resolveScene(g_base);
    if (!scene) return;
    const auto player = resolvePlayerActors(scene.value);
    if (!player) return;
    const auto* component = read<const void*>(registry(reinterpret_cast<const void*>(player.value.player.address)), 0x40);
    const auto* resource = read<const void*>(component, 0x18);
    if (read<const void*>(resource, 0x18) != controller) return;
    if (!std::isfinite(frames) || frames < 0 || frames >= 100) { refused(Refusal::Clock, std::bit_cast<std::uint32_t>(frames)); return; }
    if (g_timeLock.test_and_set(std::memory_order_acquire)) return;
    g_poseTime = advancePoseTime(g_poseTime, gale::channelEpoch(), simulationTick(), frames);
    g_time.store((std::uint64_t(g_poseTime.channel) << 32) | std::bit_cast<std::uint32_t>(g_poseTime.frames), std::memory_order_release);
    g_timeLock.clear(std::memory_order_release);
}

HOOK_DEFINE_TRAMPOLINE(AnimationCalc) {
    static std::uintptr_t Callback(void* controller, float frames) {
        const auto result = Orig(controller, frames);
        if (!(read<unsigned>(controller, 0x124) & 0x60000)) observeAnimation(controller, read<float>(controller, 0x11C));
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(AnimationSimple) {
    static std::uintptr_t Callback(void* controller, float frames) {
        const auto result = Orig(controller, frames);
        if (!(read<unsigned>(controller, 0x124) & 0x60000)) observeAnimation(controller, frames);
        return result;
    }
};
void uploadPose(void* unit) {
    if (!hinted(unit)) return;
    Lease lease;
    if (!prepare(unit, lease)) return;
    const auto buffer = read<std::uint8_t>(unit, kBufferIndex) & 3u;
    native<void(*)(void*, unsigned)>(g_base, 0x824A8)(lease.work->input.skeleton, buffer);
    native<void(*)(void*, unsigned)>(g_base, 0x74F450)(lease.work->input.model, buffer);
}

using NativeBounds = float (*)(void*);
void poseBounds(void* unit, NativeBounds calculate) {
    if (!hinted(unit)) return;
    Lease lease;
    if (!prepare(unit, lease)) return;
    auto& work = *lease.work;
    if (work.target.relative)
        for (unsigned bone = 0; bone < work.target.identity.boneCount; ++bone)
            if (!rebasePoseOrigin(work.output[bone], work.target.origin, {})) { refused(Refusal::Transform, bone); return; }
    work.bounds.prepare(unit, work.input);
    (void)calculate(work.bounds.unit);
}

// Integer register `index` of the hooked code; `context->X[]` does not work on a float context.
std::uint64_t gameRegister(const exl::hook::InlineFloatCtx* context, unsigned index) {
    return *reinterpret_cast<const std::uint64_t*>(
        reinterpret_cast<const std::uint8_t*>(context) + 0x200 + index * 8);
}

HOOK_DEFINE_TRAMPOLINE(BeforeDraw) {
    static std::uintptr_t Callback(void* unit, void* context, const void* views, int count) {
        const auto result = Orig(unit, context, views, count);
        uploadPose(unit);
        return result;
    }
};
HOOK_DEFINE_INLINE(InlineDrawResume143) {
    static void Callback(exl::hook::InlineFloatCtx* context) {
        uploadPose(reinterpret_cast<void*>(gameRegister(context, 19)));
    }
};
HOOK_DEFINE_TRAMPOLINE(CalculateView) {
    static void Callback(void* model, unsigned view, const void* camera, unsigned buffer) {
        const auto* unit = reinterpret_cast<const void*>(
            reinterpret_cast<std::uintptr_t>(model) - std::uintptr_t(std::ptrdiff_t(kLayout.embeddedModel)));
        if (!hinted(unit)) { Orig(model, view, camera, buffer); return; }
        Lease lease;
        if (prepare(unit, lease)) Orig(lease.work->input.model, view, camera, buffer);
        else Orig(model, view, camera, buffer);
    }
};
HOOK_DEFINE_TRAMPOLINE(CalculateBounds) {
    static float Callback(void* unit) {
        const float result = Orig(unit);
        poseBounds(unit, [](void* copy) { return Orig(copy); });
        return result;
    }
};
// The game has already stored the live unit's bounds here; the posed copy's bounds replace them.
HOOK_DEFINE_INLINE(FoldedBounds143) {
    static void Callback(exl::hook::InlineFloatCtx* context) {
        poseBounds(reinterpret_cast<void*>(gameRegister(context, 19)),
                   [](void* copy) { return CalculateBounds::Orig(copy); });
    }
};
} // namespace

void publish(const SceneContext& scene, const ActorHandle& player) {
    g_count.store(0, std::memory_order_release);
    // With pose rendering off (see install) the game's model lists are not read.
    if (!g_installed) return;
    if (!gale::active()) { g_controller.store(0, std::memory_order_release); return; }
    std::array<std::uintptr_t, kPublishedLimit> units{};
    unsigned count = 0;
    const auto append = [&](const void* actor) {
        return visitModels(actor, [&](const void* unit) {
            const auto address = reinterpret_cast<std::uintptr_t>(unit);
            for (unsigned i = 0; i < count; ++i) if (units[i] == address) return true;
            if (count == units.size()) return false;
            units[count++] = address;
            return true;
        });
    };
    const auto* actor = reinterpret_cast<const void*>(player.address);
    const auto* as = read<const void*>(registry(actor), 0x40);
    const auto* resource = read<const void*>(as, 0x18);
    g_controller.store(read<std::uintptr_t>(resource, 0x18), std::memory_order_release);
    const bool valid = append(actor) && visitLinks(actor, [&](const void* link) {
        if (!link) return true;
        ActorReference reference(g_base, link);
        const auto* owner = reference.get();
        if (!owner) return true;
        if (!append(owner)) return false;
        if (const auto* fused = fuseLink(owner)) {
            ActorReference child(g_base, fused);
            if (child.get() && !append(child.get())) return false;
        }
        return true;
    });
    if (!valid) { refused(Refusal::Capacity, count); return; }
    for (unsigned i = 0; i < count; ++i) g_models[i].store(units[i], std::memory_order_relaxed);
    g_scene.store(scene.token.value, std::memory_order_release);
    g_count.store(count, std::memory_order_release);
}

bool install(std::uintptr_t base) {
    // On 1.4.3 a failed check turns off only the pose; the rest of Revali's Gale keeps running.
    const bool is143 = totk::port::is143();
    if (!totk::port::codeMatches(base, kSites, "champions",
            is143 ? "Gale pose rendering off on 1.4.3:" : "GALE_RENDER_BUILD_REFUSED")) return is143;
    if (is143 && !codeMatches(base, kResumeSites143, "champions", "Gale pose rendering off on 1.4.3:")) return true;
    g_base = base;
    AnimationCalc::InstallAtOffset(0x6333C8_v121);
    AnimationSimple::InstallAtOffset(0x833618_v121);
    BeforeDraw::InstallAtOffset(0x76B6EC_v121);
    CalculateView::InstallAtOffset(0x76C078_v121);
    CalculateBounds::InstallAtOffset(0x756E98_v121);
    if (is143) {
        InlineDrawResume143::InstallAtOffset(kDrawResume143);
        FoldedBounds143::InstallAtOffset(kBoundsResume143);
        Logging.Log("[champions] Gale pose rendering on 1.4.3: unit 0x308, model +0xD8, skeleton +0x110, "
                    "inline resumes +%#lx +%#lx", static_cast<unsigned long>(kDrawResume143),
                    static_cast<unsigned long>(kBoundsResume143));
    }
    g_installed = true;
    return true;
}
} // namespace botw_abilities::champions::pose
