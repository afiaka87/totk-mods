// SPDX-License-Identifier: MIT
#include <lib.hpp>
#include "AtlasGpu.hpp"
#include "AtlasRenderer.hpp"
#include "FidelityShaders.hpp"
#include "LabelAtlas.hpp"
#if SURVEY_STARTUP_DIAGNOSTIC
#include "SurveyStartupDiagnostic.hpp"
#endif
#include <cstring>

namespace zonai_survey::atlas_gpu {
namespace {
namespace shader=survey_fidelity::shaders;
constexpr std::size_t align(std::size_t n,std::size_t a) { return (n+a-1)&~(a-1); }
constexpr auto kFragment=align(sizeof(shader::labelVertCode),256);
constexpr auto kCodeEnd=kFragment+sizeof(shader::labelFragCode);
constexpr auto kSlots=align(kCodeEnd+16384,4096);
constexpr auto kPoolBytes=kSlots+atlas::kSlotCount*atlas::kSlotBytes;
static_assert(kPoolBytes<=256*1024);
alignas(4096) unsigned char g_storage[kPoolBytes]{};
NVNmemoryPool g_pool{};
NVNmemoryPool g_atlasPool{};
NVNprogram g_program{};
NVNsync g_fences[atlas::kSlotCount]{};
bool g_pending[atlas::kSlotCount]{};
bool g_ready{}, g_attempted{};
NVNbufferAddress g_address{};
NVNbufferAddress g_atlasAddress{};
unsigned g_next{},g_refusal{},g_peakQuads{},g_busy{};
std::uint64_t g_frames{},g_lastReport{},g_cpuTicks{},g_completed{};
PFNNVNMEMORYPOOLFLUSHMAPPEDRANGEPROC g_flush{};
PFNNVNCOMMANDBUFFERBINDPROGRAMPROC g_bind{};
PFNNVNCOMMANDBUFFERBINDUNIFORMBUFFERPROC g_uniform{};
PFNNVNCOMMANDBUFFERDRAWARRAYSPROC g_draw{};
PFNNVNCOMMANDBUFFERFENCESYNCPROC g_fence{};
PFNNVNCOMMANDBUFFERBARRIERPROC g_barrier{};
PFNNVNSYNCWAITPROC g_wait{};
template<class T> T read(const void* p,std::size_t offset) { T v; std::memcpy(&v,static_cast<const unsigned char*>(p)+offset,sizeof(v)); return v; }
void mark(const char* event,std::uint64_t a=0,std::uint64_t b=0) {
    Logging.Log("[survey-atlas] %s a=%llu b=%llu\n",event,a,b);
#if SURVEY_STARTUP_DIAGNOSTIC
    engine::startup_diagnostic::mark(event,a,b);
#endif
}
void refuse(unsigned why,unsigned detail=0) { if(g_refusal!=why) { g_refusal=why; mark("atlas-refused",why,detail); } }
template<class F> bool resolve(F& target,NVNdevice* d,PFNNVNDEVICEGETPROCADDRESSPROC get,const char* name) {
    target=reinterpret_cast<F>(get(d,name));
    if(!target) Logging.Log("[survey-atlas] missing %s\n",name);
    return target!=nullptr;
}
bool initialize(NVNdevice* device,PFNNVNDEVICEGETPROCADDRESSPROC get) {
    if(g_ready) return true;
    if(g_attempted) return false;
    g_attempted=true;
    mark("atlas-init-begin",atlas::kPixelBytes,kPoolBytes+atlas::kPixelPoolBytes);
    PFNNVNMEMORYPOOLBUILDERSETDEFAULTSPROC defaults{};
    PFNNVNMEMORYPOOLBUILDERSETDEVICEPROC setDevice{};
    PFNNVNMEMORYPOOLBUILDERSETSTORAGEPROC storage{};
    PFNNVNMEMORYPOOLBUILDERSETFLAGSPROC flags{};
    PFNNVNMEMORYPOOLINITIALIZEPROC initPool{};
    PFNNVNMEMORYPOOLGETBUFFERADDRESSPROC address{};
    PFNNVNDEVICEGETINTEGERPROC integer{};
    PFNNVNPROGRAMINITIALIZEPROC initProgram{};
    PFNNVNPROGRAMSETSHADERSPROC shaders{};
    PFNNVNSYNCINITIALIZEPROC initSync{};
#define LOAD(target,name) if(!resolve(target,device,get,name)) { refuse(1); return false; }
    LOAD(defaults,"nvnMemoryPoolBuilderSetDefaults")
    LOAD(setDevice,"nvnMemoryPoolBuilderSetDevice")
    LOAD(storage,"nvnMemoryPoolBuilderSetStorage")
    LOAD(flags,"nvnMemoryPoolBuilderSetFlags")
    LOAD(initPool,"nvnMemoryPoolInitialize")
    LOAD(address,"nvnMemoryPoolGetBufferAddress")
    LOAD(integer,"nvnDeviceGetInteger")
    LOAD(initProgram,"nvnProgramInitialize")
    LOAD(shaders,"nvnProgramSetShaders")
    LOAD(initSync,"nvnSyncInitialize")
    LOAD(g_flush,"nvnMemoryPoolFlushMappedRange")
    LOAD(g_bind,"nvnCommandBufferBindProgram")
    LOAD(g_uniform,"nvnCommandBufferBindUniformBuffer")
    LOAD(g_draw,"nvnCommandBufferDrawArrays")
    LOAD(g_fence,"nvnCommandBufferFenceSync")
    LOAD(g_barrier,"nvnCommandBufferBarrier")
    LOAD(g_wait,"nvnSyncWait")
#undef LOAD
    int padding=-1,limit=0;
    integer(device,NVN_DEVICE_INFO_SHADER_CODE_MEMORY_POOL_PADDING_SIZE,&padding);
    integer(device,NVN_DEVICE_INFO_MAX_UNIFORM_BUFFER_SIZE,&limit);
    mark("atlas-driver-limits",padding,limit);
    if(padding<0 || kCodeEnd+padding>kSlots || limit<int(atlas::kFontBankBytes)) { refuse(2,limit); return false; }
    std::memcpy(g_storage,shader::labelVertCode,sizeof(shader::labelVertCode));
    std::memcpy(g_storage+kFragment,shader::labelFragCode,sizeof(shader::labelFragCode));
    NVNmemoryPoolBuilder builder{};
    defaults(&builder); setDevice(&builder,device); storage(&builder,g_storage,kPoolBytes);
    flags(&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED|NVN_MEMORY_POOL_FLAGS_SHADER_CODE);
    if(!initPool(&g_pool,&builder)) { refuse(3); return false; }
    g_address=address(&g_pool);
    if(!g_address) { refuse(4); return false; }
    g_flush(&g_pool,0,kSlots);
    storage(&builder,atlas::kPixels,atlas::kPixelPoolBytes);
    flags(&builder,NVN_MEMORY_POOL_FLAGS_CPU_CACHED|NVN_MEMORY_POOL_FLAGS_GPU_CACHED);
    if(!initPool(&g_atlasPool,&builder)) { refuse(10); return false; }
    g_atlasAddress=address(&g_atlasPool);
    if(!g_atlasAddress) { refuse(11); return false; }
    g_flush(&g_atlasPool,0,atlas::kPixelPoolBytes);
    if(!initProgram(&g_program,device)) { refuse(5); return false; }
    const NVNshaderData stages[]{{g_address,shader::labelVertControl},{g_address+kFragment,shader::labelFragControl}};
    if(!shaders(&g_program,2,stages)) { refuse(6); return false; }
    for(unsigned i=0;i<atlas::kSlotCount;++i) if(!initSync(&g_fences[i],device)) { refuse(7,i); return false; }
    g_ready=true;
    mark("atlas-ready",kPoolBytes+atlas::kPixelPoolBytes,atlas::kSlotCount*atlas::kSlotBytes);
    u64 used{},total{};
    const auto a=svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
    const auto b=svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);
    mark("atlas-process-memory",total,used); mark("atlas-memory-results",a,b);
    return true;
}
}
void draw(std::uintptr_t base,NVNdevice* device,PFNNVNDEVICEGETPROCADDRESSPROC get,
          void* drawContext,void* context,const float* view,const float* projection) {
    auto* command=read<NVNcommandBuffer*>(drawContext,0xb8);
    if(!command) { refuse(8); return; }
    if(!initialize(device,get)) return;
    unsigned slot=atlas::kSlotCount;
    for(unsigned i=0;i<atlas::kSlotCount;++i) {
        const unsigned candidate=(g_next+i)%atlas::kSlotCount;
        if(g_pending[candidate]) {
            const auto result=g_wait(&g_fences[candidate],0);
            if(result==NVN_SYNC_WAIT_RESULT_FAILED) { refuse(9,candidate); return; }
            if(result==NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED) continue;
            g_pending[candidate]=false; ++g_completed;
        }
        slot=candidate; break;
    }
    if(slot==atlas::kSlotCount) { ++g_busy; if(g_busy==1 || g_busy%300==0) mark("atlas-slots-busy",g_busy,g_frames); return; }
    const auto start=svcGetSystemTick();
    const auto offset=kSlots+slot*atlas::kSlotBytes;
    auto* quads=reinterpret_cast<atlas::Quad*>(g_storage+offset);
    const unsigned count=render::buildAtlasQuads(view,projection,quads);
    if(!count) return;
    if(count>g_peakQuads) g_peakQuads=count;
    g_flush(&g_pool,offset,align(count*sizeof(atlas::Quad),256));
    alignas(8) unsigned char state[128]{};
    const auto defaults=reinterpret_cast<void(*)(void*)>(base+0x74c19c);
    const auto apply=reinterpret_cast<void(*)(void*,void*)>(base+0x756a08);
    defaults(state); state[0]=state[1]=0;
    // GraphicsContext::apply reads cull face at +101; screen-space quads are two-sided.
    state[101]=NVN_FACE_NONE;
    const unsigned blend=1; std::memcpy(state+4,&blend,4);
    state[40]=NVN_BLEND_FUNC_SRC_ALPHA; state[42]=NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA;
    state[41]=NVN_BLEND_FUNC_ZERO; state[43]=NVN_BLEND_FUNC_ONE;
    state[44]=state[45]=NVN_BLEND_EQUATION_ADD;
    apply(state,drawContext);
    reinterpret_cast<void(*)(void*,void*)>(base+0xc5a6fc)(context,drawContext);
    g_barrier(command,NVN_BARRIER_INVALIDATE_SHADER_BIT);
    g_bind(command,&g_program,63);
    g_uniform(command,NVN_SHADER_STAGE_FRAGMENT,0,g_atlasAddress,atlas::kFontBankBytes);
    for(unsigned first=0;first<count;first+=atlas::kBatchQuads) {
        const unsigned batch=std::min(atlas::kBatchQuads,count-first);
        g_uniform(command,NVN_SHADER_STAGE_VERTEX,0,g_address+offset+first*sizeof(atlas::Quad),atlas::kBatchQuads*sizeof(atlas::Quad));
        g_draw(command,NVN_DRAW_PRIMITIVE_TRIANGLES,0,batch*6);
    }
    g_fence(command,&g_fences[slot],NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE,0);
    g_pending[slot]=true; g_next=(slot+1)%atlas::kSlotCount;
    reinterpret_cast<void(*)(void*,void*)>(base+0x962c18)(context,drawContext);
    reinterpret_cast<void(*)(void*,void*)>(base+0xc4b7ac)(read<void*>(context,0x540),drawContext);
    defaults(state); apply(state,drawContext);
    ++g_frames; g_cpuTicks+=svcGetSystemTick()-start;
    if(!g_lastReport || start-g_lastReport>=19200000*5ull) {
        mark("atlas-frame-quads",g_frames,count); mark("atlas-cost",g_cpuTicks,g_peakQuads*sizeof(atlas::Quad));
        mark("atlas-markers",render::lastGlyphsDrawn(),render::lastNamesDropped());
        mark("atlas-fence-reuse",g_busy,g_completed);
        unsigned letters=0,icons=0;
        for(unsigned i=0;i<count;++i) {
            if(quads[i].tile[1]==16) ++letters;
            else if(quads[i].tile[1]==32) ++icons;
        }
        mark("atlas-letter-icon-quads",letters,icons);
        g_lastReport=start;
    }
    g_refusal=0;
}
}
