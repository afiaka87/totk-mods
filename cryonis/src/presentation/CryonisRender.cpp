// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisRender.hpp"
#include "CryonisRuntime.hpp"
#include "CryonisHooks.hpp"
#include "CryonisEngine.hpp"
#include "CryonisShaders.hpp"
#include "CryonisAssets.hpp"
#include "CryonisGrowth.hpp"
#include "CryonisRenderPolicy.hpp"
#include "totk/render/PfxHook.hpp"
#include <lib.hpp>
#include "Totk121Signatures.hpp"
#include <nvn/nvn.h>
#include <cmath>
#include <cstring>
#include <algorithm>
namespace cryonis::render {
namespace {
using render_policy::Camera;
using render_policy::ray;
using render_policy::Uniforms;
using render_policy::uniforms;
using engine::call;
template <class T> T read(const void *p, std::size_t offset) {
    T v{};
    if (p)
        std::memcpy(&v, static_cast<const unsigned char *>(p) + offset, sizeof(v));
    return v;
}
template <class T> void write(void *p, std::size_t offset, T v) {
    std::memcpy(static_cast<unsigned char *>(p) + offset, &v, sizeof(v));
}
void *variable(std::uintptr_t offset) {
    return read<void *>(reinterpret_cast<void *>(engine::moduleBase), offset);
}
std::uint32_t g_refusals{};
void refuse(unsigned reason) {
    if (reason < 32 && !(g_refusals & (1u << reason))) {
        g_refusals |= 1u << reason;
        Logging.Log("[cryonis] RENDER_REFUSED reason=%u", reason);
    }
}
NVNdevice *g_device{};
PFNNVNDEVICEGETPROCADDRESSPROC g_getProc{};
template <class F> bool resolve(F &f, const char *name) {
    f = reinterpret_cast<F>(g_getProc(g_device, name));
    if (!f)
        Logging.Log("[cryonis] NVN_MISSING %s", name);
    return f != nullptr;
}
struct Blob {
    const unsigned char *code;
    std::size_t size;
    const unsigned char *control;
};
#define BLOB(name) {shaders::name##Code, sizeof(shaders::name##Code), shaders::name##Control}
constexpr Blob kBlobs[]{BLOB(watervert),   BLOB(waterfrag),   BLOB(icevert), BLOB(icefrag),
                        BLOB(effectsvert), BLOB(effectsfrag), BLOB(hudvert), BLOB(hudfrag)};
#undef BLOB
alignas(4096) unsigned char g_codeMemory[128 * 1024]{};
alignas(4096) unsigned char g_meshMemory[53248]{};
alignas(65536) unsigned char g_textureMemory[8 * 1024 * 1024]{};
NVNmemoryPool g_codePool{}, g_meshPool{}, g_texturePool{};
NVNprogram g_waterProgram{}, g_iceProgram{}, g_effectsProgram{}, g_hudProgram{};
NVNtexture g_textures[3]{};
int g_textureIds[3]{-1, -1, -1};
int g_textureSampler{-1};
NVNbufferAddress g_meshAddress{}, g_indexAddress{};
PFNNVNCOMMANDBUFFERBINDPROGRAMPROC g_bindProgram{};
PFNNVNCOMMANDBUFFERBINDUNIFORMBUFFERPROC g_bindUniform{};
PFNNVNCOMMANDBUFFERDRAWARRAYSPROC g_drawArrays{};
PFNNVNCOMMANDBUFFERDRAWELEMENTSPROC g_drawElements{};
PFNNVNCOMMANDBUFFERBINDTEXTUREPROC g_bindTexture{};
PFNNVNDEVICEGETTEXTUREHANDLEPROC g_textureHandle{};
PFNNVNCOMMANDBUFFERSETSCISSORPROC g_scissor{};
PFNNVNCOMMANDBUFFERSETVIEWPORTPROC g_viewport{};
PFNNVNCOMMANDBUFFERBARRIERPROC g_barrier{};
alignas(8) unsigned char g_samplerBindings[128]{};
std::uint32_t g_samplerLocations[4]{0xffff00ff, 0, 0, 0};
bool g_initialized{}, g_attempted{};
bool camera(const void *context, Camera &c) {
    const auto record = read<const unsigned char *>(context, 0x18);
    if (!record)
        return false;
    std::memcpy(c.view, record + 0x100, sizeof(c.view));
    std::memcpy(c.projection, record + 0x160, sizeof(c.projection));
    c.near = read<float>(record, 0x1E0);
    c.far = read<float>(record, 0x1E4);
    for (const auto value : c.view)
        if (!std::isfinite(value))
            return false;
    for (const auto value : c.projection)
        if (!std::isfinite(value))
            return false;
    return c.near > 0 && c.far > c.near && std::isfinite(c.far) &&
           std::fabs(c.projection[0]) > .0001f && std::fabs(c.projection[5]) > .0001f;
}
bool initializePrograms() {
    auto graphics = variable(offsets::kGraphicsDevice);
    g_getProc =
        reinterpret_cast<PFNNVNDEVICEGETPROCADDRESSPROC>(variable(offsets::kNvnGetProcAddress));
    if (!graphics || !g_getProc) {
        refuse(1);
        return false;
    }
    g_device = read<NVNdevice *>(graphics, 0x30);
    if (!g_device) {
        refuse(2);
        return false;
    }
    g_attempted = true;
    PFNNVNMEMORYPOOLBUILDERSETDEFAULTSPROC defaults{};
    PFNNVNMEMORYPOOLBUILDERSETDEVICEPROC device{};
    PFNNVNMEMORYPOOLBUILDERSETSTORAGEPROC storage{};
    PFNNVNMEMORYPOOLBUILDERSETFLAGSPROC flags{};
    PFNNVNMEMORYPOOLINITIALIZEPROC init{};
    PFNNVNMEMORYPOOLFLUSHMAPPEDRANGEPROC flush{};
    PFNNVNMEMORYPOOLGETBUFFERADDRESSPROC address{};
    PFNNVNPROGRAMINITIALIZEPROC programInit{};
    PFNNVNPROGRAMSETSHADERSPROC shadersSet{};
    PFNNVNDEVICEGETINTEGERPROC integer{};
    if (!resolve(defaults, "nvnMemoryPoolBuilderSetDefaults") ||
        !resolve(device, "nvnMemoryPoolBuilderSetDevice") ||
        !resolve(storage, "nvnMemoryPoolBuilderSetStorage") ||
        !resolve(flags, "nvnMemoryPoolBuilderSetFlags") ||
        !resolve(init, "nvnMemoryPoolInitialize") ||
        !resolve(flush, "nvnMemoryPoolFlushMappedRange") ||
        !resolve(address, "nvnMemoryPoolGetBufferAddress") ||
        !resolve(programInit, "nvnProgramInitialize") ||
        !resolve(shadersSet, "nvnProgramSetShaders") || !resolve(integer, "nvnDeviceGetInteger") ||
        !resolve(g_bindProgram, "nvnCommandBufferBindProgram") ||
        !resolve(g_bindUniform, "nvnCommandBufferBindUniformBuffer") ||
        !resolve(g_drawArrays, "nvnCommandBufferDrawArrays") ||
        !resolve(g_drawElements, "nvnCommandBufferDrawElements") ||
        !resolve(g_bindTexture, "nvnCommandBufferBindTexture") ||
        !resolve(g_textureHandle, "nvnDeviceGetTextureHandle") ||
        !resolve(g_scissor, "nvnCommandBufferSetScissor") ||
        !resolve(g_viewport, "nvnCommandBufferSetViewport") ||
        !resolve(g_barrier, "nvnCommandBufferBarrier")) {
        refuse(3);
        return false;
    }
    NVNmemoryPoolBuilder builder{};
    const auto makePool = [&](NVNmemoryPool &pool, void *bytes, std::size_t size, int extra) {
        defaults(&builder);
        device(&builder, g_device);
        storage(&builder, bytes, size);
        flags(&builder,
              NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED | extra);
        return init(&pool, &builder);
    };
    std::size_t offsets[8]{}, used = 0;
    for (unsigned i = 0; i < 8; ++i) {
        used = (used + 255) & ~std::size_t(255);
        offsets[i] = used;
        if (used + kBlobs[i].size > sizeof(g_codeMemory)) {
            refuse(4);
            return false;
        }
        std::memcpy(g_codeMemory + used, kBlobs[i].code, kBlobs[i].size);
        used += kBlobs[i].size;
    }
    int padding = -1;
    integer(g_device, NVN_DEVICE_INFO_SHADER_CODE_MEMORY_POOL_PADDING_SIZE, &padding);
    if (padding < 0 || used + std::size_t(padding) > sizeof(g_codeMemory)) {
        refuse(4);
        return false;
    }
    if (!makePool(g_codePool, g_codeMemory, sizeof(g_codeMemory),
                  NVN_MEMORY_POOL_FLAGS_SHADER_CODE) ||
        !makePool(g_meshPool, g_meshMemory, sizeof(g_meshMemory), 0) ||
        !makePool(g_texturePool, g_textureMemory, sizeof(g_textureMemory), 0)) {
        refuse(5);
        return false;
    }
    flush(&g_codePool, 0, sizeof(g_codeMemory));
    const auto codeAddress = address(&g_codePool);
    g_meshAddress = address(&g_meshPool);
    if (!codeAddress || !g_meshAddress) {
        refuse(6);
        return false;
    }
    std::memcpy(g_meshMemory, assets::mesh, sizeof(assets::mesh));
    constexpr std::size_t indexOffset = (sizeof(assets::mesh) + 255) & ~std::size_t(255);
    static_assert(indexOffset + sizeof(assets::indices) <= sizeof(g_meshMemory));
    std::memcpy(g_meshMemory + indexOffset, assets::indices, sizeof(assets::indices));
    g_indexAddress = g_meshAddress + indexOffset;
    flush(&g_meshPool, 0, sizeof(g_meshMemory));
    NVNprogram *programs[]{&g_waterProgram, &g_iceProgram, &g_effectsProgram, &g_hudProgram};
    for (unsigned pass = 0; pass < 4; ++pass) {
        auto program = programs[pass];
        const unsigned first = pass * 2;
        if (!programInit(program, g_device)) {
            refuse(7);
            return false;
        }
        const NVNshaderData stages[]{{codeAddress + offsets[first], kBlobs[first].control},
                                     {codeAddress + offsets[first + 1], kBlobs[first + 1].control}};
        if (!shadersSet(program, 2, stages)) {
            refuse(8);
            return false;
        }
    }
    return true;
}
bool initializeTextures() {
    PFNNVNTEXTUREBUILDERSETDEFAULTSPROC texDefaults{};
    PFNNVNTEXTUREBUILDERSETDEVICEPROC texDevice{};
    PFNNVNTEXTUREBUILDERSETTARGETPROC texTarget{};
    PFNNVNTEXTUREBUILDERSETSIZE2DPROC texSize{};
    PFNNVNTEXTUREBUILDERSETFORMATPROC texFormat{};
    PFNNVNTEXTUREBUILDERGETSTORAGESIZEPROC texBytes{};
    PFNNVNTEXTUREBUILDERGETSTORAGEALIGNMENTPROC texAlignment{};
    PFNNVNTEXTUREBUILDERSETSTORAGEPROC texStorage{};
    PFNNVNTEXTUREINITIALIZEPROC texInit{};
    PFNNVNTEXTUREWRITETEXELSPROC texWrite{};
    PFNNVNTEXTUREFLUSHTEXELSPROC texFlush{};
    if (!resolve(texDefaults, "nvnTextureBuilderSetDefaults") ||
        !resolve(texDevice, "nvnTextureBuilderSetDevice") ||
        !resolve(texTarget, "nvnTextureBuilderSetTarget") ||
        !resolve(texSize, "nvnTextureBuilderSetSize2D") ||
        !resolve(texFormat, "nvnTextureBuilderSetFormat") ||
        !resolve(texBytes, "nvnTextureBuilderGetStorageSize") ||
        !resolve(texAlignment, "nvnTextureBuilderGetStorageAlignment") ||
        !resolve(texStorage, "nvnTextureBuilderSetStorage") ||
        !resolve(texInit, "nvnTextureInitialize") || !resolve(texWrite, "nvnTextureWriteTexels") ||
        !resolve(texFlush, "nvnTextureFlushTexels")) {
        refuse(3);
        return false;
    }
    const unsigned widths[]{assets::tex0Width, assets::tex1Width, assets::tex2Width},
        heights[]{assets::tex0Height, assets::tex1Height, assets::tex2Height};
    const unsigned char *pixels[]{assets::tex0, assets::tex1, assets::tex2};
    auto manager = read<void *>(variable(offsets::kNvnManagerSlot), 0);
    if (!manager) {
        refuse(9);
        return false;
    }
    std::size_t used = 0;
    for (unsigned i = 0; i < 3; ++i) {
        NVNtextureBuilder tb{};
        texDefaults(&tb);
        texDevice(&tb, g_device);
        texTarget(&tb, NVN_TEXTURE_TARGET_2D);
        texSize(&tb, widths[i], heights[i]);
        texFormat(&tb, NVN_FORMAT_RGBA8);
        const auto alignment = texAlignment(&tb), bytes = texBytes(&tb);
        if (!alignment || (alignment & (alignment - 1)) || !bytes) {
            refuse(10);
            return false;
        }
        used = (used + alignment - 1) & ~(alignment - 1);
        if (used + bytes > sizeof(g_textureMemory)) {
            refuse(10);
            return false;
        }
        texStorage(&tb, &g_texturePool, used);
        used += bytes;
        if (!texInit(&g_textures[i], &tb)) {
            refuse(11);
            return false;
        }
        NVNcopyRegion region{0, 0, 0, int(widths[i]), int(heights[i]), 1};
        texWrite(&g_textures[i], nullptr, &region, pixels[i]);
        texFlush(&g_textures[i], nullptr, &region);
        g_textureIds[i] = call<int (*)(void *, NVNtexture *, NVNtextureView *)>(
            offsets::kNVNMgrRegisterTexture)(manager, &g_textures[i], nullptr);
        if (g_textureIds[i] < 0) {
            refuse(12);
            return false;
        }
    }
    write<std::uint32_t>(g_samplerBindings, 0x68, 1);
    write<const void *>(g_samplerBindings, 0x70, g_samplerLocations);
    Logging.Log("[cryonis] GPU_READY mesh=%u indices=%u texture_bytes=%u", assets::vertexCount,
                assets::indexCount, unsigned(used));
    return true;
}
bool initialize() {
    if (g_initialized)
        return true;
    if (g_attempted)
        return false;
    if (!initializePrograms() || !initializeTextures())
        return false;
    g_initialized = true;
    return true;
}

struct DrawResources {
    NVNcommandBuffer *command{};
    void *sampler{};
    unsigned width{}, height{};
};
bool resources(void *drawContext, void *scene, void *context, DrawResources &out) {
    if (!drawContext || !scene || !context || !read<void *>(context, 0x540) ||
        !read<void *>(context, 0x548)) {
        refuse(13);
        return false;
    }
    auto command = read<NVNcommandBuffer *>(drawContext, 0xB8);
    auto buffers = read<const void *>(scene, 0x1EC0);
    if (!command || !buffers || read<unsigned>(buffers, 8) < 1) {
        refuse(14);
        return false;
    }
    auto record = read<unsigned char *>(buffers, 0x10);
    const auto f = read<unsigned>(record, 8);
    const unsigned slot = f & 1 ? 0x700 : f & 2 ? 0xE40 : 0;
    if (!record || !slot) {
        refuse(15);
        return false;
    }
    void *sampler = record + slot;
    const auto width = read<std::uint16_t>(sampler, 0x30),
               height = read<std::uint16_t>(sampler, 0x32);
    if (width < 16 || height < 16 || width > 8192 || height > 8192) {
        refuse(16);
        return false;
    }
    out = {command, sampler, width, height};
    return true;
}
struct DrawBlocks {
    void *common{}, *grid{}, *marker{}, *pillars[4]{}, *effects[7]{};
    unsigned pillarCount{}, effectCount{};
};
bool upload(const Camera &c, const Presentation &snapshot, unsigned width, unsigned height,
            DrawBlocks &out) {
    auto common = uniforms(c, snapshot, width, height);
    common.base[0] = snapshot.player.x;
    common.base[1] = snapshot.player.y;
    common.base[2] = snapshot.player.z;
    common.base[3] = snapshot.aiming ? 1 : 0;
    if (snapshot.aiming && snapshot.target.valid && snapshot.target.pillar < 0) {
        common.appearance[0] = snapshot.target.base.x;
        common.appearance[1] = snapshot.target.base.y;
        common.appearance[2] = snapshot.target.base.z;
        common.appearance[3] = 1;
    }
    bool good = true;
    const auto create = [&](void *&block, const void *data, std::size_t size) {
        call<void (*)(void **, const void *, std::size_t)>(offsets::kUtlCreateDynamicUniformBlock)(
            &block, data, size);
        good &= block && read<std::uint64_t>(block, 0x78);
    };
    if (snapshot.aiming)
        create(out.grid, snapshot.water, sizeof(snapshot.water));
    const auto pillars = render_policy::pillars(common, snapshot, c);
    if (common.appearance[3] > .5f) {
        auto marker = common;
        marker.waterOrigin[3] = 0;
        create(out.marker, &marker, sizeof(marker));
    }
    create(out.common, &common, sizeof(common));
    for (unsigned i = 0; i < pillars.count; ++i)
        create(out.pillars[i], &pillars.values[i], sizeof(Uniforms));
    const auto effect = [&](Vec3 base, float age, float height, float kind, float yaw) {
        auto u = common;
        u.base[0] = base.x;
        u.base[1] = base.y;
        u.base[2] = base.z;
        u.transform[0] = std::cos(yaw);
        u.transform[1] = std::sin(yaw);
        u.appearance[0] = age;
        u.appearance[2] = height;
        u.appearance[3] = kind;
        u.flags[1] = age;
        create(out.effects[out.effectCount++], &u, sizeof(u));
    };
    for (const auto &p : snapshot.pillars)
        if (p.occupied) {
            const float top = std::max(0.0f, emergenceHeight(p.growth.frame));
            effect(p.base, p.growth.frame, top, 0, p.yaw);
            if (!p.growth.finished())
                effect(p.base, p.growth.frame, top, 1, p.yaw);
        }
    if (snapshot.breakAge >= 0 && snapshot.breakAge < 35)
        effect(snapshot.broken, snapshot.breakAge, kHeight, 2, 0);
    out.pillarCount = pillars.count;
    if (!good)
        refuse(18);
    return good;
}
bool bindTextures(void *drawContext, void *sampler, NVNcommandBuffer *command) {
    if (!call<bool (*)(void *, void *, unsigned, void *)>(offsets::kShaderProgramActivateSampler)(
            g_samplerBindings, drawContext, 0, sampler)) {
        refuse(19);
        return false;
    }
    if (g_textureSampler < 0) {
        // Native AGL copies this 40-byte descriptor into its sampler pool.
        alignas(8) unsigned char descriptor[40];
        std::memcpy(descriptor, static_cast<unsigned char *>(sampler) + 184, sizeof(descriptor));
        write<float>(descriptor, 16, 0);
        write<float>(descriptor, 20, 0);
        write<float>(descriptor, 24, 0);
        descriptor[28] = NVN_MAG_FILTER_LINEAR;
        descriptor[29] = NVN_MIN_FILTER_LINEAR;
        descriptor[30] = 0;
        descriptor[31] = descriptor[32] = descriptor[33] = NVN_WRAP_MODE_REPEAT;
        descriptor[34] = 1;
        descriptor[36] = NVN_SAMPLER_REDUCTION_AVERAGE;
        write<std::uint16_t>(descriptor, 38, 0);
        auto manager = read<void *>(variable(offsets::kNvnManagerSlot), 0);
        if (!manager) {
            refuse(20);
            return false;
        }
        const auto id = call<unsigned (*)(void *, const void *, const char *)>(
            offsets::kNVNMgrRegisterSampler)(manager, descriptor, "Cryonis original textures");
        if (id >= 0xffff) {
            refuse(20);
            return false;
        }
        g_textureSampler = int(id);
    }
    g_barrier(command, NVN_BARRIER_INVALIDATE_TEXTURE_DESCRIPTOR_BIT);
    for (unsigned i = 0; i < 3; ++i)
        g_bindTexture(command, NVN_SHADER_STAGE_FRAGMENT, i + 1,
                      g_textureHandle(g_device, g_textureIds[i], g_textureSampler));
    return true;
}
void issuePasses(void *drawContext, void *context, const DrawResources &resources,
                 const DrawBlocks &blocks, const Presentation &snapshot) {
    const auto command = resources.command;
    const auto width = resources.width, height = resources.height;
    alignas(8) unsigned char state[128]{};
    call<void (*)(void *)>(offsets::kGraphicsContextGraphicsContext)(state);
    state[0] = state[1] = 0;
    // Water faces must be visible from above; ice rejects its own rear faces.
    state[101] = NVN_FACE_NONE;
    write<unsigned>(state, 4, 1);
    state[40] = NVN_BLEND_FUNC_ONE;
    state[41] = NVN_BLEND_FUNC_ZERO;
    state[42] = NVN_BLEND_FUNC_ONE_MINUS_SRC_ALPHA;
    state[43] = NVN_BLEND_FUNC_ONE;
    state[44] = state[45] = NVN_BLEND_EQUATION_ADD;
    call<void (*)(void *, void *)>(offsets::kModelRenderContextBindDefaultFrameBuffer)(context,
                                                                                       drawContext);
    call<void (*)(void *, void *)>(offsets::kGraphicsContextApply)(state, drawContext);
    g_viewport(command, 0, 0, float(width), float(height));
    g_scissor(command, 0, 0, width, height);
    const auto bindBlock = [&](unsigned binding, const void *block) {
        for (const auto stage : {NVN_SHADER_STAGE_VERTEX, NVN_SHADER_STAGE_FRAGMENT})
            g_bindUniform(command, stage, binding, read<std::uint64_t>(block, 0x78),
                          read<std::uint32_t>(block, 0x38));
    };
    if (snapshot.aiming) {
        bindBlock(0, blocks.common);
        g_bindUniform(command, NVN_SHADER_STAGE_VERTEX, 2, read<std::uint64_t>(blocks.grid, 0x78),
                      sizeof(snapshot.water));
        g_bindProgram(command, &g_waterProgram, 63);
        g_drawArrays(command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 63 * 63 * 6);
    }
    if (blocks.marker) {
        bindBlock(0, blocks.marker);
        g_bindProgram(command, &g_waterProgram, 63);
        g_drawArrays(command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 6);
    }
    g_bindUniform(command, NVN_SHADER_STAGE_VERTEX, 1, g_meshAddress, sizeof(assets::mesh));
    g_bindProgram(command, &g_iceProgram, 63);
    for (unsigned i = 0; i < blocks.pillarCount; ++i) {
        bindBlock(0, blocks.pillars[i]);
        g_drawElements(command, NVN_DRAW_PRIMITIVE_TRIANGLES, NVN_INDEX_TYPE_UNSIGNED_SHORT,
                       assets::indexCount, g_indexAddress);
    }
    g_bindProgram(command, &g_effectsProgram, 63);
    for (unsigned i = 0; i < blocks.effectCount; ++i) {
        bindBlock(0, blocks.effects[i]);
        g_drawArrays(command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 48 * 6);
    }
    bindBlock(0, blocks.common);
    g_bindProgram(command, &g_hudProgram, 63);
    g_drawArrays(command, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    call<void (*)(void *, void *)>(offsets::kModelRenderContextUnbindDefaultFrameBuffer)(
        context, drawContext);
    call<void (*)(void *, void *)>(offsets::kRestoreRenderBuffers)(read<void *>(context, 0x540),
                                                                   drawContext);
    call<void (*)(void *)>(offsets::kGraphicsContextGraphicsContext)(state);
    call<void (*)(void *, void *)>(offsets::kGraphicsContextApply)(state, drawContext);
}
void draw(void *drawContext, void *scene, void *context, const Camera &c) {
    Presentation snapshot;
    if (!runtime::presentation(snapshot))
        return;
    bool any = snapshot.aiming || snapshot.cooldown > 0 ||
               (snapshot.breakAge >= 0 && snapshot.breakAge < 35);
    for (const auto p : snapshot.pillars)
        any |= p.occupied;
    if (!any)
        return;
    DrawResources r;
    if (!resources(drawContext, scene, context, r))
        return;
    if (!initialize())
        return;
    auto allocator = variable(offsets::kUniformAllocator);
    if (!allocator || !read<void *>(allocator, 24) || !read<unsigned>(allocator, 72)) {
        refuse(17);
        return;
    }
    DrawBlocks blocks;
    if (!upload(c, snapshot, r.width, r.height, blocks))
        return;
    if (!bindTextures(drawContext, r.sampler, r.command))
        return;
    issuePasses(drawContext, context, r, blocks, snapshot);
    static bool reported{};
    if (!reported) {
        Logging.Log("[cryonis] DRAW_READY %u,%u pillars=%u water=%u", r.width, r.height,
                    blocks.pillarCount, unsigned(snapshot.aiming));
        reported = true;
    }
}
struct Pfx {
    inline static totk::render::PfxCallback previous{};
    static std::uint64_t Callback(void *extension, void *args) {
        const auto result = previous(extension, args);
        if (!args || read<unsigned>(args, 0x18) != 1 || read<unsigned>(args, 0x1C) != 0)
            return result;
        auto context = read<void *>(args, 0x10);
        if (!context || read<std::uint8_t>(context, 8) != 0)
            return result;
        Camera c{};
        const bool ready = camera(context, c);
        engine::publishCameraRay(runtime::hooks(), ready ? ray(c) : Ray{});
        if (ready)
            draw(read<void *>(args, 0), read<void *>(args, 8), context, c);
        return result;
    }
};
}
void install(std::uintptr_t base) {
    if (!signatures::codeMatches(base, signatures::kRender, "RENDER_REFUSED"))
        return;
    if (totk::render::installPfxHook(
            base, {signatures::kPfx.offset, signatures::kPfx.first, signatures::kPfx.second},
            Pfx::Callback, Pfx::previous, "cryonis"))
        Logging.Log("[cryonis] PFX installed");
}
}
