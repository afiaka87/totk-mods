// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "RagdollTransport.hpp"
#include "ActorReference.hpp"
#include "FlightClock.hpp"
#include "../pure/RagdollTransport.hpp"
#include "../pure/FlightDiagnostics.hpp"
#include "../program/modules/arrowbound/HookshotRuntime.hpp"
#include "../program/modules/arrowbound/HookshotLog.hpp"
#include "totk/engine/Scene.hpp"
#include "totk/engine/Pointer.hpp"
#include <lib.hpp>
#include <arrowbound/ActiveGame.hpp>

namespace arrowbound::ragdoll_transport {
namespace {
std::uintptr_t base{};
const profiles::Game* game{};
std::atomic<std::uint32_t> generation{};
std::atomic_flag busy = ATOMIC_FLAG_INIT;
std::atomic<unsigned> bindingCount{}, faults{};
std::array<std::atomic<std::uintptr_t>,64> candidateBodies{},candidateWorlds{};
std::array<std::atomic<std::uint64_t>,64> candidateHandles{};
struct Binding {
    std::uintptr_t body{}, world{};
    std::uint64_t handle{};
    pure::SpeedLimitLease lease;
    bool root{};
    unsigned clamps{}, updates{};
};
struct State {
    pure::RagdollTransport transport;
    std::uintptr_t player{}, structure{};
    std::uint32_t shot{}, generation{};
    unsigned samples{}, count{};
    std::array<Binding,64> bodies{};
} state;
template<class T> T read(std::uintptr_t pointer, std::size_t offset) {
    T value;
    std::memcpy(&value,reinterpret_cast<const void*>(pointer+offset),sizeof(value));
    return value;
}
template<class T> void write(std::uintptr_t pointer, std::size_t offset, const T& value) {
    std::memcpy(reinterpret_cast<void*>(pointer+offset),&value,sizeof(value));
}
bool valid(std::uintptr_t pointer) { return totk::engine::isPlausibleAddress(pointer); }
void reject(const char* reason, unsigned value=0) {
    const auto n=faults.fetch_add(1)+1;
    if (n<=8 || n%120==0) ZHLOG("RAGDOLL_FLIGHT_REJECT reason=%s value=%u count=%u",reason,value,n);
}
struct Lock {
    bool held=!busy.test_and_set(std::memory_order_acquire);
    ~Lock() { if (held) busy.clear(std::memory_order_release); }
};
bool candidate(std::uintptr_t body) {
    const auto count=bindingCount.load(std::memory_order_acquire);
    for (unsigned i=0;i<count;++i)
        if (candidateBodies[i].load(std::memory_order_relaxed)==body) return true;
    return false;
}
bool candidate(std::uintptr_t world, std::uint64_t handle) {
    const auto count=bindingCount.load(std::memory_order_acquire);
    for (unsigned i=0;i<count;++i)
        if (candidateWorlds[i].load(std::memory_order_relaxed)==world &&
            candidateHandles[i].load(std::memory_order_relaxed)==handle) return true;
    return false;
}
bool presenting() {
    const auto& drive=runtime().drive;
    const auto pose=drive.poseState.load(std::memory_order_acquire);
    return drive.presentParaglider.load(std::memory_order_acquire) &&
        drive.parasailActive.load(std::memory_order_acquire) &&
        ((pose>>8)&0xFF)==1 && ((pose>>16)&0xFF)==4;
}
bool allowed() {
    return state.transport.engaged && presenting() &&
        state.shot==runtime().arrow.shotSeq.load(std::memory_order_acquire) &&
        state.generation==generation.load(std::memory_order_acquire);
}
engine::ActorReference playerReference() {
    const auto scene=totk::engine::resolveScene(base);
    if (!scene) { reject("scene"); return {}; }
    using PlayerLink=const void* (*)(std::uintptr_t);
    const auto* link=reinterpret_cast<PlayerLink>(base+game->calls.getPlayerActor)(scene.value.residentActorManager);
    if (!link) { reject("player_link"); return {}; }
    using Resolve=engine::ActorReference (*)(const void*);
    return reinterpret_cast<Resolve>(base+game->calls.getReference)(link);
}
std::uintptr_t structureFor(std::uintptr_t player) {
    using GetSet=std::uintptr_t (*)(std::uintptr_t);
    using GetStructure=std::uintptr_t (*)(std::uintptr_t,unsigned);
    const auto set=reinterpret_cast<GetSet>(base+game->physics.getControllerSet.offset)(player);
    return valid(set) ? reinterpret_cast<GetStructure>(base+game->physics.getStructure.offset)(set,0) : 0;
}
std::uintptr_t worldFor(std::uintptr_t body) {
    // The physics engine variable holds the engine pointer (one dereference).
    const auto engine=read<std::uintptr_t>(base,game->variables.physics);
    if (!valid(engine)) return 0;
    const auto physics=read<std::uintptr_t>(engine,0xC8);
    if (!valid(physics)) return 0;
    const auto layer=(read<std::uint64_t>(body,0x68)>>5)&1;
    const auto wrapper=read<std::uintptr_t>(physics,0xC0+8*layer);
    return valid(wrapper) ? read<std::uintptr_t>(wrapper,game->layout.worldFromWrapper) : 0;
}
void clearIdentity(std::uintptr_t player, std::uintptr_t structure) {
    if (state.count) ZHLOG("RAGDOLL_FLIGHT_RETIRE reason=identity count=%u",state.count);
    state={};
    state.player=player;
    state.structure=structure;
    bindingCount.store(0,std::memory_order_release);
}
bool currentOwner(const Binding& binding) {
    const auto reference=playerReference();
    if (!valid(reference.actor) || reference.actor!=state.player) return false;
    const auto structure=structureFor(reference.actor);
    if (!valid(structure) || structure!=state.structure) return false;
    const auto count=read<unsigned>(structure,0x58);
    const auto entries=read<std::uintptr_t>(structure,0x60);
    if (count<2 || count>65 || !valid(entries)) return false;
    for (unsigned i=0;i+1<count;++i) {
        const auto body=read<std::uintptr_t>(entries+0x98u*i,0x28);
        if (body!=binding.body || !valid(body)) continue;
        const auto sdk=read<std::uintptr_t>(body,0x70);
        return valid(sdk) && read<std::uint64_t>(sdk,8)==binding.handle && worldFor(body)==binding.world;
    }
    return false;
}
// Queue the current velocity so the backend restores its limit even when velocity is unchanged.
void queueRestore(Binding& binding, std::uintptr_t body) {
    if (!binding.lease.owned) return;
    using Added=bool (*)(std::uintptr_t);
    if (!reinterpret_cast<Added>(base+game->physics.isAddedToWorld.offset)(body)) {
        reject("restore_body_detached");
        binding.lease={};
        return;
    }
    using Mutex=void (*)(std::uintptr_t);
    using GetRequest=std::uintptr_t (*)(std::uintptr_t);
    reinterpret_cast<Mutex>(base+game->physics.bodyLock.offset)(body+0x78);
    const auto request=reinterpret_cast<GetRequest>(base+game->physics.getOrAllocRequest.offset)(body);
    if (valid(request)) {
        auto& flags=*reinterpret_cast<std::uint32_t*>(request+0xD4);
        if (!(flags&0x80)) {
            const auto velocity=read<pure::Vec3>(body,0xF8);
            if (pure::finite3(velocity)) {
                write(request,0x44,velocity);
                __atomic_fetch_or(&flags,0x80u,__ATOMIC_RELAXED);
                __atomic_fetch_and(reinterpret_cast<std::uint32_t*>(request+0xD0),~0x20u,__ATOMIC_RELAXED);
            } else reject("restore_velocity");
        }
    } else reject("restore_request");
    reinterpret_cast<Mutex>(base+game->physics.bodyUnlock.offset)(body+0x78);
}
void retireCurrent(std::uintptr_t player, std::uintptr_t structure) {
    state.transport={};
    if (player!=state.player || structure!=state.structure) {
        clearIdentity(player,structure);
        return;
    }
    const auto count=read<unsigned>(structure,0x58);
    const auto entries=read<std::uintptr_t>(structure,0x60);
    if (count<2 || count>65 || !valid(entries)) { reject("restore_layout",count); return; }
    for (unsigned i=0;i+1<count;++i) {
        const auto body=read<std::uintptr_t>(entries+0x98u*i,0x28);
        if (!valid(body)) continue;
        for (unsigned j=0;j<state.count;++j)
            if (state.bodies[j].body==body) queueRestore(state.bodies[j],body);
    }
    bool pending=false;
    for (unsigned j=0;j<state.count;++j) pending|=state.bodies[j].lease.owned;
    if (!pending) {
        if (state.count) ZHLOG("RAGDOLL_FLIGHT_RESTORED shot=%u bodies=%u",state.shot,state.count);
        state.count=0;
        bindingCount.store(0,std::memory_order_release);
    }
}
void observe(std::uintptr_t structure, const float* basis, float delta) {
    if (!presenting() && !bindingCount.load(std::memory_order_acquire)) return;
    const auto reference=playerReference();
    if (!valid(reference.actor)) { reject("player"); return; }
    if (structureFor(reference.actor)!=structure) return;
    Lock lock;
    if (!lock.held) { reject("step_busy"); return; }
    if (state.player!=reference.actor || state.structure!=structure) clearIdentity(reference.actor,structure);
    const auto shot=runtime().arrow.shotSeq.load(std::memory_order_acquire);
    const auto gen=generation.load(std::memory_order_acquire);
    if (!presenting() || state.shot!=shot || state.generation!=gen) {
        retireCurrent(reference.actor,structure);
        state.shot=shot;
        state.generation=gen;
        state.samples=0;
    }
    if (!presenting()) return;
    const auto previous=state.transport;
    // A failed refresh must not leave last frame's body exception armed.
    state.transport.engaged=false;
    if (!basis || !std::isfinite(delta) || delta<=0) { reject("delta_or_basis"); return; }
    for (unsigned i=0;i<12;++i)
        if (!std::isfinite(basis[i])) { reject("basis_nonfinite",i); return; }
    const auto count=read<unsigned>(structure,0x58);
    const auto entries=read<std::uintptr_t>(structure,0x60);
    const auto mapping=read<std::uintptr_t>(structure,0x40);
    const auto source=read<std::uintptr_t>(structure,0x48);
    if (count<2 || count>65 || !valid(entries) || !valid(mapping) || !valid(source)) { reject("layout",count); return; }
    using FindBody=std::uint16_t (*)(std::uintptr_t,const char* const*);
    const char* name="Skl_Root";
    const auto root=reinterpret_cast<FindBody>(base+game->physics.findBodyByName.offset)(structure,&name);
    const auto mappedCount=read<unsigned>(mapping,8);
    const auto mapped=read<std::uintptr_t>(mapping,0x10);
    if (root>=count-1 || root>=mappedCount || mappedCount>64 || !valid(mapped)) { reject("root_mapping",mappedCount); return; }
    using Refresh=void (*)(std::uintptr_t,std::uintptr_t);
    reinterpret_cast<Refresh>(base+game->physics.refreshMapping.offset)(mapping,source);
    const auto target=pure::transformPoint(basis,read<pure::Vec3>(mapped+120u*root,0x18));
    std::array<pure::RagdollBodyPose,64> poses{};
    std::array<Binding,64> fresh{};
    unsigned used=0,anchor=64;
    for (unsigned i=0;i+1<count;++i) {
        const auto body=read<std::uintptr_t>(entries+0x98u*i,0x28);
        if (!body) continue;
        if (!valid(body)) { reject("body",i); return; }
        for (unsigned j=0;j<used;++j)
            if (fresh[j].body==body) { reject("duplicate_body",i); return; }
        const auto sdk=read<std::uintptr_t>(body,0x70);
        const auto world=worldFor(body);
        if (!valid(sdk) || !valid(world)) { reject("backend",i); return; }
        const auto table=read<std::uintptr_t>(world,0);
        if (!valid(table) || read<std::uintptr_t>(table,0x198)!=base+game->physics.setBodyLinearVelocity.offset) { reject("world_writer",i); return; }
        fresh[used].body=body;
        fresh[used].world=world;
        fresh[used].handle=read<std::uint64_t>(sdk,8);
        fresh[used].root=i==root;
        if (i==root) anchor=used;
        using GetMatrix=std::uint64_t (*)(std::uintptr_t,float*);
        using GetVelocity=void (*)(std::uintptr_t,pure::Vec3*);
        reinterpret_cast<GetMatrix>(base+game->physics.getCurrentMatrix.offset)(body,poses[used].matrix.data());
        reinterpret_cast<GetVelocity>(base+game->physics.getNextLinearVelocity.offset)(body,&poses[used].velocity);
        ++used;
    }
    auto planned=previous;
    const auto result=planned.prepare(std::span(poses.data(),used),anchor,target);
    if (result==pure::TransportResult::Invalid) { reject("pose",used); return; }
    if (result==pure::TransportResult::Fast) {
        for (unsigned i=0;i<used;++i) for (unsigned j=0;j<state.count;++j) {
            const auto& old=state.bodies[j];
            if (old.body==fresh[i].body && old.world==fresh[i].world && old.handle==fresh[i].handle) {
                fresh[i].lease=old.lease;
                fresh[i].clamps=old.clamps;
                fresh[i].updates=old.updates;
            }
        }
        state.bodies=fresh;
        state.count=used;
        for (unsigned i=0;i<used;++i) {
            candidateBodies[i].store(fresh[i].body,std::memory_order_relaxed);
            candidateWorlds[i].store(fresh[i].world,std::memory_order_relaxed);
            candidateHandles[i].store(fresh[i].handle,std::memory_order_relaxed);
        }
        bindingCount.store(used,std::memory_order_release);
    }
    state.transport=planned;
    const auto n=++state.samples;
    if (n<=30 || n%6==0) {
        pure::FlightTime clock{};
        game_clock::snapshot(clock);
        const auto position=poses[anchor].position();
        ZHLOG("RAGDOLL_FLIGHT shot=%u sample=%u fast=%u serial=%llu gap_cm=%d speed_cm_s=%d target_cm=(%d,%d,%d) body_cm=(%d,%d,%d)",
            shot,n,unsigned(planned.engaged),(unsigned long long)clock.serial,pure::traceNumber(planned.gap),
            pure::traceNumber(pure::length(poses[anchor].velocity)),pure::traceNumber(target.x),pure::traceNumber(target.y),
            pure::traceNumber(target.z),pure::traceNumber(position.x),pure::traceNumber(position.y),pure::traceNumber(position.z));
    }
}
HOOK_DEFINE_TRAMPOLINE(VelocityClampHook) {
    static bool Callback(std::uintptr_t body, pure::Vec3* velocity) {
        const auto incoming=*velocity;
        const bool native=Orig(body,velocity);
        if (!candidate(body)) return native;
        Lock lock;
        if (!lock.held) { reject("clamp_busy"); return native; }
        if (!allowed()) return native;
        for (unsigned i=0;i<state.count;++i) {
            auto& binding=state.bodies[i];
            if (binding.body!=body) continue;
            const auto speed=pure::length(incoming);
            if (!pure::finite3(incoming) || !std::isfinite(speed)) { reject("velocity_nonfinite"); return native; }
            const auto nativeSpeed=pure::length(*velocity);
            *velocity=pure::boundedFlightVelocity(incoming);
            const auto n=++binding.clamps;
            if (binding.root && (n<=30 || n%6==0))
                ZHLOG("RAGDOLL_SPEED_REQUEST shot=%u n=%u wanted_cm_s=%d native_cm_s=%d allowed_cm_s=%d",
                    state.shot,n,pure::traceNumber(speed),pure::traceNumber(nativeSpeed),pure::traceNumber(pure::length(*velocity)));
            return speed<=pure::RagdollTransport::kSpeedCeiling;
        }
        return native;
    }
};
// 1.4.x inlines the clamp: after the native write, replace the queued request velocity under the body lock.
void overrideRequest(std::uintptr_t body, const pure::Vec3& incoming) {
    if (!candidate(body)) return;
    Lock lock;
    if (!lock.held) { reject("request_busy"); return; }
    if (!allowed()) return;
    for (unsigned i=0;i<state.count;++i) {
        auto& binding=state.bodies[i];
        if (binding.body!=body) continue;
        const auto speed=pure::length(incoming);
        if (!pure::finite3(incoming) || !std::isfinite(speed)) { reject("velocity_nonfinite"); return; }
        // Flag 0x100 routes the request to a shared queue entry; leave that one alone.
        if (read<std::uint64_t>(body,0x68)&0x100) { reject("request_shared"); return; }
        using Mutex=void (*)(std::uintptr_t);
        reinterpret_cast<Mutex>(base+game->physics.bodyLock.offset)(body+0x78);
        const auto request=read<std::uintptr_t>(body,0x60);
        pure::Vec3 native{};
        bool written=false;
        if (valid(request) && (read<std::uint32_t>(request,0xD4)&0x80)) {
            native=read<pure::Vec3>(request,0x44);
            write(request,0x44,pure::boundedFlightVelocity(incoming));
            written=true;
        }
        const auto wrote=written ? read<pure::Vec3>(request,0x44) : native;
        reinterpret_cast<Mutex>(base+game->physics.bodyUnlock.offset)(body+0x78);
        if (!written) { reject("request_missing"); return; }
        const auto n=++binding.clamps;
        if (binding.root && (n<=30 || n%6==0))
            ZHLOG("RAGDOLL_SPEED_REQUEST shot=%u n=%u wanted_cm_s=%d native_cm_s=%d allowed_cm_s=%d",
                state.shot,n,pure::traceNumber(speed),pure::traceNumber(pure::length(native)),
                pure::traceNumber(pure::length(wrote)));
        return;
    }
}
HOOK_DEFINE_TRAMPOLINE(RequestVelocityHook) {
    static std::uint64_t Callback(std::uintptr_t body, pure::Vec3* velocity) {
        const auto incoming=velocity ? *velocity : pure::Vec3{};
        const auto result=Orig(body,velocity);
        if (velocity) overrideRequest(body,incoming);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(RequestVelocityWrapperHook) {
    static std::uint64_t Callback(std::uintptr_t wrapper, pure::Vec3* velocity) {
        const auto incoming=velocity ? *velocity : pure::Vec3{};
        const auto result=Orig(wrapper,velocity);
        if (velocity && valid(wrapper)) overrideRequest(read<std::uintptr_t>(wrapper,24),incoming);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(WorldVelocityHook) {
    static std::uint64_t Callback(std::uintptr_t world, std::uint64_t handle, const void* velocity, int activation) {
        std::uintptr_t observedMotion=0;
        unsigned observedShot=0,observedUpdate=0;
        if (candidate(world,handle)) {
            Lock lock;
            if (!lock.held) reject("world_busy");
            else for (unsigned i=0;i<state.count;++i) {
                auto& binding=state.bodies[i];
                if (binding.world!=world || binding.handle!=handle) continue;
                if (!currentOwner(binding)) {
                    reject("world_owner_changed");
                    binding.lease={};
                    break;
                }
                using Get=std::uintptr_t (*)(std::uintptr_t,std::uint64_t);
                const auto motion=reinterpret_cast<Get>(base+game->physics.getSpeedLimit.offset)(world,handle);
                if (!valid(motion)) { reject("motion"); break; }
                const auto current=read<float>(motion,12);
                if (!std::isfinite(current) || current<=0) { reject("native_limit"); break; }
                const bool active=allowed();
                const auto next=binding.lease.update(motion,current,active);
                if (next!=current) {
                    write(motion,12,next);
                    ZHLOG("RAGDOLL_SPEED_LIMIT shot=%u root=%u active=%u from_cm_s=%d to_cm_s=%d",
                        state.shot,unsigned(binding.root),unsigned(active),pure::traceNumber(current),pure::traceNumber(next));
                }
                ++binding.updates;
                if (binding.root && (binding.updates<=30 || binding.updates%6==0)) {
                    observedMotion=motion;
                    observedShot=state.shot;
                    observedUpdate=binding.updates;
                }
                break;
            }
        }
        const auto result=Orig(world,handle,velocity,activation);
        if (observedMotion) {
            const auto applied=read<pure::Vec3>(observedMotion,0);
            ZHLOG("RAGDOLL_SPEED_APPLIED shot=%u n=%u speed_cm_s=%d limit_cm_s=%d",
                observedShot,observedUpdate,pure::traceNumber(pure::length(applied)),
                pure::traceNumber(read<float>(observedMotion,12)));
        }
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(RagdollStepHook) {
    static std::uint64_t Callback(std::uintptr_t structure, const float* matrix, float delta) {
        observe(structure,matrix,delta);
        return Orig(structure,matrix,delta);
    }
};
}
void reset() { generation.fetch_add(1,std::memory_order_acq_rel); }
void retireIfInactive() {
    if (!base || !bindingCount.load(std::memory_order_acquire)) return;
    Lock lock;
    if (!lock.held) { reject("retire_busy"); return; }
    if (allowed()) return;
    const auto reference=playerReference();
    if (!valid(reference.actor)) { reject("retire_player"); return; }
    const auto structure=structureFor(reference.actor);
    if (!valid(structure)) { reject("retire_structure"); return; }
    retireCurrent(reference.actor,structure);
}
void install(std::uintptr_t mainBase) {
    game=profiles::active();
    if (!game) return;
    base=mainBase;
    const auto& p=game->physics;
    const profiles::Site guards[]={p.ragdollStep,p.getControllerSet,p.getStructure,p.findBodyByName,
        p.refreshMapping,p.getCurrentMatrix,p.getNextLinearVelocity,p.velocityClamp,
        p.setBodyLinearVelocity,p.getSpeedLimit,p.getOrAllocRequest,p.isAddedToWorld,p.bodyLock,
        p.bodyUnlock,p.requestVelocity,p.requestVelocityWrapper};
    for (const auto& guard:guards) {
        if (!guard.offset) continue;
        const auto word=read<std::uint32_t>(base,guard.offset);
        if (word!=guard.word) {
            ZHLOG("HOOK DISABLED ragdoll flight offset=%x word=%08x expected=%08x",unsigned(guard.offset),word,guard.word);
            base=0;
            return;
        }
    }
    if (p.velocityClamp.offset) {
        VelocityClampHook::InstallAtOffset(p.velocityClamp.offset);
    } else if (p.requestVelocity.offset && p.requestVelocityWrapper.offset) {
        RequestVelocityHook::InstallAtOffset(p.requestVelocity.offset);
        RequestVelocityWrapperHook::InstallAtOffset(p.requestVelocityWrapper.offset);
    } else {
        ZHLOG("HOOK DISABLED ragdoll flight: no velocity request site");
        base=0;
        return;
    }
    WorldVelocityHook::InstallAtOffset(p.setBodyLinearVelocity.offset);
    RagdollStepHook::InstallAtOffset(p.ragdollStep.offset);
    ZHLOG("RAGDOLL_FLIGHT_READY game=%s threshold_cm=800 ceiling_cm_s=500000 matrix_writes=0",game->name);
}
}
