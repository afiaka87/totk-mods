// SPDX-License-Identifier: MIT
#include <lib.hpp>
#include "AtlasGpu.hpp"
#include "AtlasRenderer.hpp"
#include "SurveyShaders.hpp"
#include "LabelAtlas.hpp"
#include "SurveyGameProfiles.hpp"
#include <cstring>

namespace zonai_survey::atlas_gpu {
namespace {
namespace shader=zonai_survey::shaders;
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
unsigned g_lastSlot{},g_lastCount{};
NVNbufferAddress g_address{};
NVNbufferAddress g_atlasAddress{};
unsigned g_next{},g_refusal{};
PFNNVNMEMORYPOOLFLUSHMAPPEDRANGEPROC g_flush{};
PFNNVNCOMMANDBUFFERBINDPROGRAMPROC g_bind{};
PFNNVNCOMMANDBUFFERBINDUNIFORMBUFFERPROC g_uniform{};
PFNNVNCOMMANDBUFFERDRAWARRAYSPROC g_draw{};
PFNNVNCOMMANDBUFFERFENCESYNCPROC g_fence{};
PFNNVNCOMMANDBUFFERBARRIERPROC g_barrier{};
PFNNVNSYNCWAITPROC g_wait{};
template<class T> T read(const void* p,std::size_t offset) { T v; std::memcpy(&v,static_cast<const unsigned char*>(p)+offset,sizeof(v)); return v; }
template<class F> F native(std::uintptr_t base,std::ptrdiff_t offset) { return reinterpret_cast<F>(base+offset); }
using ContextCall=void(*)(void*,void*);
void refuse(unsigned why,unsigned detail=0) {
    if(g_refusal!=why) { g_refusal=why; Logging.Log("[survey-atlas] refused reason=%u detail=%u\n",why,detail); }
}
template<class F> bool resolve(F& target,NVNdevice* d,PFNNVNDEVICEGETPROCADDRESSPROC get,const char* name) {
    target=reinterpret_cast<F>(get(d,name));
    if(!target) Logging.Log("[survey-atlas] missing %s\n",name);
    return target!=nullptr;
}
bool initialize(NVNdevice* device,PFNNVNDEVICEGETPROCADDRESSPROC get) {
    if(g_ready) return true;
    if(g_attempted) return false;
    g_attempted=true;
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
    if(padding<0 || kCodeEnd+padding>kSlots || limit<int(atlas::kPixelBytes)) { refuse(2,limit); return false; }
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
    Logging.Log("[survey-atlas] ready\n");
    return true;
}
struct Batch { NVNcommandBuffer* command{}; unsigned slot{}; std::size_t offset{}; unsigned count{}; };
bool prepare(NVNdevice* device,PFNNVNDEVICEGETPROCADDRESSPROC get,void* drawContext,
             const float* view,const float* projection,const atlas::PixelGrid& grid,Batch& batch) {
    batch.command=read<NVNcommandBuffer*>(drawContext,0xb8);
    if(!batch.command) { refuse(8); return false; }
    if(!initialize(device,get)) return false;
    unsigned slot=atlas::kSlotCount;
    for(unsigned i=0;i<atlas::kSlotCount;++i) {
        const unsigned candidate=(g_next+i)%atlas::kSlotCount;
        if(g_pending[candidate]) {
            const auto result=g_wait(&g_fences[candidate],0);
            if(result==NVN_SYNC_WAIT_RESULT_FAILED) { refuse(9,candidate); return false; }
            if(result==NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED) continue;
            g_pending[candidate]=false;
        }
        slot=candidate; break;
    }
    // Every slot is still on the GPU: redraw the newest labels (fenced again in submit) rather than blink.
    const bool redraw=slot==atlas::kSlotCount;
    if(!redraw) {
        g_lastSlot=slot;
        g_lastCount=render::buildAtlasQuads(view,projection,grid,
            reinterpret_cast<atlas::Quad*>(g_storage+kSlots+slot*atlas::kSlotBytes));
    }
    batch.slot=g_lastSlot; batch.offset=kSlots+g_lastSlot*atlas::kSlotBytes; batch.count=g_lastCount;
    if(!batch.count) return false;
    if(!redraw) g_flush(&g_pool,batch.offset,align(batch.count*sizeof(atlas::Quad),256));
    return true;
}
void applyState(std::uintptr_t base,unsigned char* state,void* drawContext,bool labels) {
    const auto& calls=profiles::active->calls;
    native<void(*)(void*)>(base,calls.contextCtor)(state);
    if(labels) {
        state[0]=state[1]=0;
        // GraphicsContext::apply reads cull face at +101; screen-space quads are two-sided.
        state[101]=NVN_FACE_NONE;
        const unsigned blend=1; std::memcpy(state+4,&blend,4);
        state[40]=NVN_BLEND_FUNC_SRC_ALPHA; state[42]=NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA;
        state[41]=NVN_BLEND_FUNC_ZERO; state[43]=NVN_BLEND_FUNC_ONE;
        state[44]=state[45]=NVN_BLEND_EQUATION_ADD;
    }
    native<ContextCall>(base,calls.contextApply)(state,drawContext);
}
void submit(const Batch& batch) {
    g_barrier(batch.command,NVN_BARRIER_INVALIDATE_SHADER_BIT);
    g_bind(batch.command,&g_program,63);
    g_uniform(batch.command,NVN_SHADER_STAGE_FRAGMENT,0,g_atlasAddress,atlas::kPixelBytes);
    for(unsigned first=0;first<batch.count;first+=atlas::kBatchQuads) {
        const unsigned count=std::min(atlas::kBatchQuads,batch.count-first);
        g_uniform(batch.command,NVN_SHADER_STAGE_VERTEX,0,g_address+batch.offset+first*sizeof(atlas::Quad),
                  atlas::kBatchQuads*sizeof(atlas::Quad));
        g_draw(batch.command,NVN_DRAW_PRIMITIVE_TRIANGLES,0,count*6);
    }
    g_fence(batch.command,&g_fences[batch.slot],NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE,0);
    g_pending[batch.slot]=true; g_next=(batch.slot+1)%atlas::kSlotCount;
    g_refusal=0;
}
}
void draw(std::uintptr_t base,NVNdevice* device,PFNNVNDEVICEGETPROCADDRESSPROC get,
          void* drawContext,void* context,const float* view,const float* projection,
          float sceneWidth,float sceneHeight) {
    const auto& game = *profiles::active;
    Batch batch;
    if(!prepare(device,get,drawContext,view,projection,atlas::pixelGrid(sceneWidth,sceneHeight),batch)) return;
    alignas(8) unsigned char state[128]{};
    applyState(base,state,drawContext,true);
    native<ContextCall>(base,game.calls.bind)(context,drawContext);
    submit(batch);
    native<ContextCall>(base,game.calls.unbind)(context,drawContext);
    native<ContextCall>(base,game.calls.barrier)(read<void*>(context,game.layout.contextTarget),drawContext);
    applyState(base,state,drawContext,false);
}
void drawUi(std::uintptr_t base,NVNdevice* device,PFNNVNDEVICEGETPROCADDRESSPROC get,
            const profiles::UiSite& site,const UiTarget& ui,const float* view,const float* projection) {
    float size[2]{};
    native<void(*)(void*,float*,void*)>(base,site.viewportSize)(ui.viewport,size,ui.target);
    const auto grid=atlas::pixelGrid(size[0],size[1]);
    if(!grid.x) { refuse(12,unsigned(size[0]>0 && size[0]<65536 ? size[0] : 0)); return; }
    Batch batch;
    if(!prepare(device,get,ui.drawContext,view,projection,grid,batch)) return;
    alignas(8) unsigned char state[128]{};
    applyState(base,state,ui.drawContext,true);
    native<void(*)(void*,void*,void*)>(base,site.viewportApply)(ui.viewport,ui.drawContext,ui.target);
    alignas(8) unsigned char bindScratch[40]{};
    native<void(*)(void*,void*,void*,unsigned)>(base,site.renderBufferBind)(ui.target,ui.drawContext,bindScratch,0);
    submit(batch);
    // The interface binds and completes this target itself; restore its own context state.
    native<ContextCall>(base,profiles::active->calls.contextApply)(ui.restore,ui.drawContext);
}
}
