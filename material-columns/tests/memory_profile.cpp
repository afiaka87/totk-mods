// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define MATERIAL_COLUMNS_MEMORY_HOST_TEST
#include <cassert>
#include <cstdint>
struct MemoryTestHook {
    template<class... Args> static std::uintptr_t Orig(Args...) { return 77; }
    static void InstallAtOffset(std::uintptr_t) {}
};
#define HOOK_DEFINE_TRAMPOLINE(Name) struct Name : MemoryTestHook
#include "../src/native/MemoryProfile.cpp"
using namespace material_columns::native::memory;
template<class T, class Buffer> void put(Buffer& buffer, unsigned offset, T value) {
    *reinterpret_cast<T*>(reinterpret_cast<unsigned char*>(buffer.data())+offset)=value;
}
int main() {
    alignas(16) std::array<std::uintptr_t,80> pane{}, draw{}, texture{};
    const auto p=reinterpret_cast<std::uintptr_t>(pane.data());
    const auto d=reinterpret_cast<std::uintptr_t>(draw.data());
    const auto t=reinterpret_cast<std::uintptr_t>(texture.data());
    put(texture,64,std::uint64_t{8192}); put(texture,76,std::uint32_t{512});
    assert(textureBytes(t)==8704 && textureBytes(0)==0);
    registerCapture(11,p); registerCapture(11,p);
    assert(find(p)==&captures[0] && !captures[1].pane.load());
    captureEnabled(p,true);
    put(pane,88,static_cast<unsigned char>(1)); put(pane,89,static_cast<unsigned char>(255));
    put(pane,280,t); put(draw,416,std::uintptr_t{1});
    maximum(captures[0].scratch,16384); maximum(captures[0].scratch,4096);
    assert(DynamicCaptureDraw::Callback(p,d,0)==77);
    assert(captures[0].output==8704 && fadeHigh==25088 && captureLogs==1);
    DynamicCaptureDraw::Callback(p,d,0);
    assert(captureLogs==1); // No per-frame logging or resource ownership change.
    assert(captures[0].outputTexture==t);
    ReleaseTexture::Callback(0,t+16); assert(captures[0].outputTexture==t && releases==0);
    assert(ReleaseTexture::Callback(0,t)==77);
    assert(captures[0].outputTexture==0 && releases==1);
    ReleaseTexture::Callback(0,t); assert(releases==1);
    menuClosed();
    put(texture,64,std::uint64_t{32768}); put(draw,416,std::uintptr_t{0});
    DynamicCaptureDraw::Callback(p,d,0); assert(captures[0].output==8704);
    put(draw,416,std::uintptr_t{1}); put(pane,89,static_cast<unsigned char>(0));
    DynamicCaptureDraw::Callback(p,d,0); assert(captures[0].output==8704);
    assert(!privateBinding(true) && privateBinding(true) && privateBinding(false));
    forget(12); assert(find(p)); forget(11); assert(!find(p));
    registerCapture(22,p); assert(captures[0].output==0 && captures[0].scratch==0);
    assert(!captures[0].enabled);
    // The inline hook changes only pixel dimensions, preserving every other GP
    // register; unknown/inactive captures keep the game's allocation arguments.
    alignas(16) std::array<std::uintptr_t,8> frame{};
    frame[1]=p;
    exl::hook::InlineCtx context{};
    for (unsigned i=0;i<31;++i) context.X[i]=0x1000+i;
    context.X[29]=reinterpret_cast<std::uintptr_t>(frame.data())+32;
    context.X[28]=2040; context.X[23]=72;
    ScaleCaptureTarget::Callback(&context); assert(context.X[28]==2040 && context.X[23]==72);
    captureEnabled(p,true);
    const auto before=context;
    ScaleCaptureTarget::Callback(&context);
    assert(context.X[28]==816 && context.X[23]==28);
    for (unsigned i=0;i<31;++i) if(i!=28 && i!=23) assert(context.X[i]==before.X[i]);
    // A direct target is one allocation, not separate work+output storage.
    put(pane,89,static_cast<unsigned char>(255));
    captures[0].workTexture=t; captures[0].scratch=32768+512; activeTextures=1; fadeHigh=0;
    DynamicCaptureDraw::Callback(p,d,0);
    assert(captures[0].output==33280 && captures[0].workTexture==0 && activeTextures==1);
    assert(fadeHigh==33280 && captures[0].extraScratch==0);
    const auto released=releases.load();
    ReleaseTexture::Callback(0,t); assert(activeTextures==0 && releases==released+1);
    forget(22);
}
