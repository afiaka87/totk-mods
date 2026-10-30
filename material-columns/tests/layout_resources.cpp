// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <array>
#include <cassert>
#include <cstring>
#include <cstdint>
unsigned animationLookups{};
struct ResourceTestHook {
    static const void* Orig(void*, const char* layout, const char*, std::uint32_t*) {
        ++animationLookups;
        assert(std::strcmp(layout,"PaShortCutSlideList_00")==0);
        return reinterpret_cast<const void*>(1);
    }
    template<class... Args> static const void* Orig(Args...) { return nullptr; }
    static void InstallAtOffset(std::uintptr_t) {}
};
#define HOOK_DEFINE_TRAMPOLINE(Name) struct Name : ResourceTestHook
#include "../src/native/LayoutResources.cpp"
std::array<std::uint8_t,20> contents{};
int corrupt=-1, missing=-1, wrongSize=-1, readFailure=-1;
unsigned opens{}, closes{};
namespace nn::fs {
Result OpenFile(FileHandle* file,const char* path,int) {
    ++opens;
    file->index=std::strstr(path,"/ShortCut_") ? 0 : std::strstr(path,"/MCColumns_") ? 1 : std::strstr(path,"/MCSlot_") ? 3 : 2;
    return {int(file->index)!=missing};
}
Result GetFileSize(s64* size,FileHandle file) { *size=int(file.index)==wrongSize ? 21 : 20; return {true}; }
Result ReadFile(FileHandle file,std::int64_t,void* out,std::uint64_t size) {
    assert(size==contents.size());
    std::memcpy(out,contents.data(),size);
    if (int(file.index)==corrupt) static_cast<std::uint8_t*>(out)[19]^=1;
    return {int(file.index)!=readFailure};
}
void CloseFile(FileHandle) { ++closes; }
}
using namespace material_columns::native;
int main() {
    std::uint32_t animationSize=99;
    assert(!ColumnAnimationLookup::Callback(nullptr,"MCSlot_00","Loop",&animationSize));
    assert(animationSize==0 && animationLookups==0);
    assert(!ColumnAnimationLookup::Callback(nullptr,"MCSlot_00","In",nullptr));
    assert(ColumnAnimationLookup::Callback(nullptr,"MCColumns_00","Fade",&animationSize));
    assert(animationLookups==1);
    std::memcpy(contents.data(),"FLYT",4);
    for (unsigned i=4;i<contents.size();++i) contents[i]=i;
    std::size_t size{};
    constexpr auto type=material_columns::resources::layoutType;
    assert(!ColumnLayoutLookup::Callback(nullptr,&size,type,"Unrelated.bflyt") && opens==0);
    assert(!ColumnLayoutLookup::Callback(nullptr,&size,0,"ShortCut_00.bflyt") && opens==0);
    corrupt=1;
    assert(!ColumnLayoutLookup::Callback(nullptr,&size,type,"ShortCut_00.bflyt"));
    assert(assetState==3 && opens==2 && closes==2);
    corrupt=-1;
    assert(!loadAssets() && opens==2); // Never serve a partial set.
    assetState=0; wrongSize=0;
    assert(!loadAssets() && opens==3 && closes==3);
    assetState=0; wrongSize=-1; missing=0;
    assert(!loadAssets() && opens==4 && closes==3);
    assetState=0; missing=-1; readFailure=2;
    assert(!loadAssets() && opens==7 && closes==6);
    assetState=0; readFailure=-1;
    const auto* parent=ColumnLayoutLookup::Callback(nullptr,&size,type,"ShortCut_00.bflyt");
    assert(parent==material_columns::assets::parent && size==20 && opens==11 && closes==10);
    assert(ColumnLayoutLookup::Callback(nullptr,&size,type,"MCSlot_00.bflyt")==material_columns::assets::slot);
    material_columns::assets::parent[19]^=1; // Native parser may update resource caches.
    assert(ColumnLayoutLookup::Callback(nullptr,&size,type,"MCColumns_00.bflyt")==material_columns::assets::columns);
    assert(ColumnLayoutLookup::Callback(nullptr,&size,type,"ShortCut_00.bflyt")==parent && opens==11);
}
