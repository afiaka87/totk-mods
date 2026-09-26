// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "ModelVisibility.hpp"
#if ARROWBOUND_FLIGHT_DIAGNOSTICS
#include "ActorReference.hpp"
#include "../pure/ModelTrace.hpp"
#include "../pure/FlightDiagnostics.hpp"
#include "../program/modules/arrowbound/HookshotLog.hpp"
#include "totk/engine/Scene.hpp"
#include <arrowbound/ActiveGame.hpp>
#include <lib.hpp>

namespace arrowbound::model_trace {
namespace {
using namespace pure;
std::uintptr_t base{};
std::atomic<std::uint64_t> epoch{}, start{}, trackedEpoch{};
std::atomic<std::uint32_t> trace{}, trackedTrace{};
ModelCompletionJoin<> join;
std::atomic_flag collecting = ATOMIC_FLAG_INIT;
struct DrawCounts {
    std::atomic<std::uintptr_t> unit{};
    std::atomic<unsigned> before{}, visible{}, hidden{}, draw{};
};
std::array<DrawCounts,32> counts;
bool observing() {
    return trace.load(std::memory_order_acquire) && epoch.load() - start.load() < 600;
}
void reject(const char* reason, std::uint64_t frame) {
    ZHLOG("TRACE_MODEL_UNAVAILABLE id=%u epoch=%llu reason=%s", trace.load(),
          (unsigned long long)frame, reason);
}
DrawCounts* tracked(const void* unit) {
    if (!unit || !observing() || trackedTrace.load() != trace.load() ||
        trackedEpoch.load(std::memory_order_acquire) != epoch.load()) return nullptr;
    for (auto& count : counts)
        if (count.unit.load(std::memory_order_relaxed) == reinterpret_cast<std::uintptr_t>(unit)) return &count;
    return nullptr;
}
void collect(void* scene, void* queue, std::uint64_t frame, bool prepare = false) {
    if (!observing() || (frame - start.load() >= 8 && frame % 6 != 0)) return;
    if (collecting.test_and_set(std::memory_order_acquire)) { reject("collector busy", frame); return; }
    struct Unlock { ~Unlock() { collecting.clear(std::memory_order_release); } } unlock;
    const auto currentTrace = trace.load();
    const auto context = totk::engine::resolveScene(base);
    if (!context) { reject("scene unavailable", frame); return; }
    using PlayerLink = const void* (*)(std::uintptr_t);
    const auto* link = reinterpret_cast<PlayerLink>(base + 0x00B7EF98)(context.value.residentActorManager);
    if (!link) { reject("player link unavailable", frame); return; }
    using Resolve = engine::ActorReference (*)(const void*);
    const auto reference = reinterpret_cast<Resolve>(base + 0x00753530)(link);
    const auto* player = reinterpret_cast<const void*>(reference.actor);
    if (!player) { reject("player unavailable", frame); return; }
    const auto* registry = modelRead<const void*>(player, 0x228);
    const auto* component = registry ? modelRead<const void*>(registry, 0x10) : nullptr;
    const auto* root = component ? modelRead<const void*>(component, 0x28) : nullptr;
    if (!root) { reject("body model unavailable", frame); return; }
    if (modelRead<const void*>(root, 0x60) != scene) return;
    const auto n = modelRead<std::int32_t>(root, 0x20);
    const auto* entries = modelRead<const void* const*>(root, 0x28);
    if (n <= 0 || n > 32 || !entries) { reject("body model count", frame); return; }
    std::array<ModelAdmission,32> models{};
    for (int i = 0; i < n; ++i) {
        const auto* unit = entries[i] ? modelRead<const void*>(entries[i]) : nullptr;
        if (!unit || modelRead<std::uintptr_t>(unit) != base + 0x045C0570) {
            reject("unsupported body model", frame); return;
        }
        models[i].unit = reinterpret_cast<std::uintptr_t>(unit);
    }
    // Register identities before the queue runs: calcBeforeDraw is inside that queue.
    if (prepare) {
        if (trace.load() != currentTrace || epoch.load() != frame) return;
        for (int i = 0; i < n; ++i) counts[i].unit.store(models[i].unit,std::memory_order_relaxed);
        trackedTrace.store(currentTrace);
        trackedEpoch.store(frame,std::memory_order_release);
        return;
    }
    if (trackedEpoch.load() != frame || trackedTrace.load() != currentTrace) {
        reject("unpaired model frame",frame); return;
    }
    for (int i = 0; i < n; ++i) if (counts[i].unit.load() != models[i].unit) {
        reject("model identity changed during frame",frame); return;
    }
    if (!inspectModelQueue(queue, {models.data(), static_cast<std::size_t>(n)})) {
        reject("invalid completed queue", frame); return;
    }
    const auto* controller = modelRead<const void*>(registry, 0x280);
    const auto playerPos = modelRead<Vec3>(player, 0x2B4);
    if (controller) {
        const float a = modelRead<float>(controller,0x5C), b = modelRead<float>(controller,0x64);
        const float c = modelRead<float>(controller,0xA4), d = modelRead<float>(controller,0x60);
        ZHLOG("TRACE_MODEL_ALPHA id=%u epoch=%llu factors_milli=(%d,%d,%d,%d) product_milli=%d pos_cm=(%d,%d,%d)",
              currentTrace, (unsigned long long)frame, traceNumber(a,1000), traceNumber(b,1000),
              traceNumber(c,1000), traceNumber(d,1000), traceNumber(((a*b)*c)*d,1000),
              traceNumber(playerPos.x),traceNumber(playerPos.y),traceNumber(playerPos.z));
    } else reject("model alpha controller missing", frame);
    for (int i = 0; i < n; ++i) {
        const auto* unit = reinterpret_cast<const void*>(models[i].unit);
        const auto* skeleton = modelRead<const void*>(unit,0x170);
        const auto* resource = skeleton ? modelRead<const void*>(skeleton) : nullptr;
        const auto* matrices = skeleton ? modelRead<const float*>(skeleton,0x20) : nullptr;
        const auto bones = resource ? modelRead<std::uint16_t>(resource,0x38) : 0;
        const auto materials = modelRead<std::uint16_t>(unit,0x16A);
        const auto* boneBits = modelRead<const std::uint32_t*>(unit,0x140);
        const auto* materialBits = modelRead<const std::uint32_t*>(unit,0x148);
        if (!matrices || !bones || bones > 512 || materials > 512 || !boneBits || (materials && !materialBits)) {
            reject("model skeleton or visibility unavailable",frame); continue;
        }
        const auto origin = modelRead<Vec3>(unit,0x338);
        const bool relative = (modelRead<std::uint8_t>(unit,0x355)&1) != 0;
        const auto bone = modelBonePosition(matrices,origin,relative);
        ZHLOG("TRACE_MODEL_STATE id=%u epoch=%llu model=%d present=%u admitted=%u flags=%08x bones=%u/%u mats=%u/%u",
              currentTrace, (unsigned long long)frame, i, unsigned(models[i].present), unsigned(models[i].admitted),
              modelRead<std::uint32_t>(unit,0x12), visibleBitCount(boneBits,bones), unsigned(bones),
              visibleBitCount(materialBits,materials), unsigned(materials));
        ZHLOG("TRACE_MODEL_ROOT id=%u epoch=%llu model=%d relative=%u pos_cm=(%d,%d,%d) actor_error_cm=%d",
              currentTrace,(unsigned long long)frame,i,unsigned(relative),traceNumber(bone.x),traceNumber(bone.y),
              traceNumber(bone.z),traceNumber(distance(bone,playerPos)));
        const auto cull = inspectModelCull(unit);
        ZHLOG("TRACE_MODEL_CULL id=%u epoch=%llu model=%d mask=%08x bounds_shapes=%u render_flags=%08x views=%u grouped=%u sphere_present=%u sphere_valid=%u center_cm=(%d,%d,%d) radius_cm=%d actor_error_cm=%d origin_cm=(%d,%d,%d)",
              currentTrace,(unsigned long long)frame,i,cull.mask,unsigned(cull.shapes),cull.renderFlags,
              unsigned(cull.viewCount),unsigned(cull.grouped),unsigned(cull.spherePresent),unsigned(cull.sphereValid),
              traceNumber(cull.center.x),traceNumber(cull.center.y),traceNumber(cull.center.z),
              traceNumber(cull.radius), cull.sphereValid ? traceNumber(distance(cull.center,playerPos)) : INT_MIN,
              traceNumber(origin.x),traceNumber(origin.y),traceNumber(origin.z));
        // Completed cached view state; an unvisited view can retain an earlier frame's values.
        if (!cull.views || !cull.viewCount || cull.viewCount > 32) {
            reject("model view state unavailable",frame); continue;
        }
        for (unsigned view = 0; view < cull.viewCount; ++view) {
            const auto* state = static_cast<const char*>(cull.views)+0x48*view;
            ZHLOG("TRACE_MODEL_VIEW id=%u epoch=%llu model=%d view=%u accepted=%u cached_mask=%08x cached_values=(%d,%d) cached_lod=%u",
                  currentTrace,(unsigned long long)frame,i,view,(cull.mask >> view)&1,
                  modelRead<std::uint32_t>(state,0x38),traceNumber(modelRead<float>(state,0x3C),1000),
                  traceNumber(modelRead<float>(state,0x40),1000),unsigned(modelRead<std::uint8_t>(state,0x44)));
        }
    }
}
void complete(void* queue, void* context, unsigned lane) {
    if (!observing() || !modelRead<std::uint8_t>(context,8)) return;
    std::atomic_thread_fence(std::memory_order_acquire);
    auto* scene = modelRead<void*>(queue,0x40);
    const auto frame = epoch.load();
    if (!scene || reinterpret_cast<std::uintptr_t>(scene)+0x42A0 != reinterpret_cast<std::uintptr_t>(queue)) {
        if (frame % 60 == 0) reject("queue owner",frame);
        return;
    }
    const auto result = join.complete(reinterpret_cast<std::uintptr_t>(queue),frame,lane);
    if (result == ModelJoinStatus::Complete) collect(scene,queue,frame);
    else if (result != ModelJoinStatus::Waiting && frame % 60 == 0) reject("queue join",frame);
}
HOOK_DEFINE_TRAMPOLINE(TraceFrameStart) {
    static void Callback(void* manager) {
        Orig(manager);
        const auto previous = trackedEpoch.exchange(0,std::memory_order_acq_rel);
        for (unsigned i = 0; i < counts.size(); ++i) {
            auto& c = counts[i];
            if (previous && c.unit.load())
                ZHLOG("TRACE_MODEL_DRAW id=%u epoch=%llu model=%u before=%u visible=%u hidden=%u draw_entries=%u",
                      trackedTrace.load(),(unsigned long long)previous,i,c.before.load(),c.visible.load(),c.hidden.load(),c.draw.load());
            c.unit.store(0); c.before.store(0); c.visible.store(0); c.hidden.store(0); c.draw.store(0);
        }
        const auto next = epoch.fetch_add(1,std::memory_order_acq_rel)+1;
        join.beginFrame(next);
        if (observing() && next % 60 == 0)
            ZHLOG("TRACE_MODEL_HEARTBEAT id=%u epoch=%llu",trace.load(),(unsigned long long)next);
    }
};
HOOK_DEFINE_TRAMPOLINE(TraceSceneFrame) {
    static std::uintptr_t Callback(void* scene) {
        const auto frame = epoch.load();
        if (observing() && !join.registerScene(reinterpret_cast<std::uintptr_t>(scene)+0x42A0,frame))
            reject("scene registration",frame);
        collect(scene,nullptr,frame,true);
        return Orig(scene);
    }
};
HOOK_DEFINE_TRAMPOLINE(TraceSingleQueue) {
    static std::uintptr_t Callback(void* queue,void* context) {
        const auto result = Orig(queue,context); complete(queue,context,1); return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(TraceMultiQueue) {
    static void Callback(void* queue,void* context) { Orig(queue,context); complete(queue,context,2); }
};
HOOK_DEFINE_TRAMPOLINE(TraceBeforeDraw) {
    static std::uint64_t Callback(void* unit,void* context,const void* views,int viewCount) {
        const auto result = Orig(unit,context,views,viewCount);
        if (auto* c = tracked(unit)) c->before.fetch_add(1,std::memory_order_relaxed);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(TraceShapeVisible) {
    static std::uint64_t Callback(const void* renderUnit) {
        const auto result = Orig(renderUnit);
        if (observing()) if (auto* c = tracked(modelRead<const void*>(renderUnit,8)))
            (result ? c->visible : c->hidden).fetch_add(1,std::memory_order_relaxed);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(TraceShapeDraw) {
    static std::uint64_t Callback(const void* unit,void* context,unsigned view,unsigned flags) {
        if (observing()) if (auto* c = tracked(modelRead<const void*>(unit,8))) c->draw.fetch_add(1,std::memory_order_relaxed);
        return Orig(unit,context,view,flags);
    }
};
HOOK_DEFINE_TRAMPOLINE(TraceShapeArrayDraw) {
    static std::uint64_t Callback(const void* unit,void* context,unsigned view,unsigned flags) {
        if (observing()) if (auto* c = tracked(modelRead<const void*>(unit,8))) c->draw.fetch_add(1,std::memory_order_relaxed);
        return Orig(unit,context,view,flags);
    }
};
}
void begin(std::uint32_t id) { start.store(epoch.load()); trace.store(id,std::memory_order_release); }
void reset() { trace.store(0,std::memory_order_release); }
void install(std::uintptr_t mainBase) {
    // Diagnostic trace mapped for 1.2.1 only.
    const auto* game = profiles::active();
    if (!game || game->version != profiles::GameVersion::V121) return;
    base = mainBase;
    constexpr std::uint32_t offsets[]{0x973550,0x974D9C,0x970820,0x981248,0x76B6EC,0x2A43014,0x74C284,0x2A4A228};
    constexpr std::uint32_t words[]{0xD101C3FF,0xA9BA7BFD,0xD101C3FF,0x6DB923E9,0xD10283FF,0x79402C09,0xD10383FF,0xD100C3FF};
    for (unsigned i = 0; i < 8; ++i) {
        const auto actual = modelRead<std::uint32_t>(reinterpret_cast<const void*>(base+offsets[i]));
        if (actual != words[i]) { ZHLOG("HOOK DISABLED model trace offset=%x actual=%08x expected=%08x",offsets[i],actual,words[i]); return; }
    }
    TraceFrameStart::InstallAtOffset(offsets[0]); TraceSceneFrame::InstallAtOffset(offsets[1]);
    TraceSingleQueue::InstallAtOffset(offsets[2]); TraceMultiQueue::InstallAtOffset(offsets[3]);
    TraceBeforeDraw::InstallAtOffset(offsets[4]); TraceShapeVisible::InstallAtOffset(offsets[5]);
    TraceShapeDraw::InstallAtOffset(offsets[6]); TraceShapeArrayDraw::InstallAtOffset(offsets[7]);
    ZHLOG("MODEL_TRACE_READY schema=2 body_only=1 frames=600 stride=6 cull_bounds_views=1");
}
}
#endif
