// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/native/Localization.cpp"
#include "../src/pure/Identity.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::vector<unsigned char> fixture;
bool missing{}, invalidSize{};
bool settingsFail{};
unsigned opens{}, closes{};
char desiredCode[8]="ja";
std::string requested;
void u32(std::vector<unsigned char>& data, unsigned offset, std::uint32_t value) {
    for (unsigned i=0;i<4;++i) data[offset+i]=static_cast<unsigned char>(value>>(i*8));
}
std::vector<unsigned char> makePack(const char* text) {
    std::vector<unsigned char> data(20);
    std::memcpy(data.data(),"MCL1",4); u32(data,4,1);
    const auto key=material_columns::identity("group:fruit");
    u32(data,8,std::uint32_t(key)); u32(data,12,std::uint32_t(key>>32)); u32(data,16,20);
    data.insert(data.end(),text,text+std::strlen(text)+1);
    return data;
}
}
namespace nn::oe {
settings::LanguageCode GetDesiredLanguage() {
    return {{'e','n','-','U','S',0,0,0}};
}
}
namespace nn::settings::detail {
nn::Result GetLanguageCode(nn::settings::LanguageCode* result) {
    std::memcpy(result->code,desiredCode,8); return {!settingsFail};
}
}
namespace nn::fs {
Result OpenFile(FileHandle*,const char* path,int) {
    ++opens; requested=path; return {!missing || requested.find("en-US.bin")!=std::string::npos};
}
Result GetFileSize(s64* size,FileHandle) { *size=invalidSize ? 100000 : s64(fixture.size()); return {true}; }
Result ReadFile(FileHandle,std::int64_t,void* out,std::uint64_t size) {
    assert(size==fixture.size()); std::memcpy(out,fixture.data(),fixture.size()); return {true};
}
void CloseFile(FileHandle) { ++closes; }
}
int main(int argc,char** argv) {
    using namespace material_columns;
    for (const auto* code:localization::locales) assert(std::strcmp(localization::select(code),code)==0);
    assert(std::strcmp(localization::select("zh-CN"),"zh-Hans")==0);
    assert(std::strcmp(localization::select("zh-TW"),"zh-Hant")==0);
    assert(std::strcmp(localization::select(nullptr),"en-US")==0);
    assert(std::strcmp(localization::select("xx"),"en-US")==0);
    const char unterminated[8]={'e','n','-','U','S','x','x','x'};
    assert(std::strcmp(localization::select(unterminated),"en-US")==0);
    char16_t out[64]{}; unsigned length{};
    const char text[]="\xc3\xa9\xe9\xbe\x8d\xd0\xaf\xed\x95\x9c\xf0\x9f\x8c\x9f";
    assert(localization::decode(text,sizeof(text),out,length) && length==6);
    assert(out[0]==0xe9 && out[1]==0x9f8d && out[2]==0x42f && out[3]==0xd55c);
    assert(out[4]==0xd83c && out[5]==0xdf1f && !out[6]);
    for(const auto* bad:{"\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xe9\xbe","\x80","line\n"})
        assert(!localization::decode(bad,std::strlen(bad)+1,out,length));
    std::string longText(64,'a');
    assert(!localization::decode(longText.c_str(),longText.size()+1,out,length));
    fixture=makePack(text);
    localization::Pack pack;
    const auto fruit=identity("group:fruit");
    assert(pack.load(fixture.data(),fixture.size()));
    assert(std::strcmp(pack.label(fruit,"fallback"),text)==0);
    assert(std::strcmp(pack.label(1,"fallback"),"fallback")==0);
    for(std::size_t size=0;size<fixture.size();++size) assert(!pack.load(fixture.data(),size));
    auto corrupt=fixture; u32(corrupt,16,0xffffffff);
    assert(!pack.load(corrupt.data(),corrupt.size()));
    assert(std::strcmp(pack.label(fruit,"fallback"),"fallback")==0);
    native::locale::loadOnce(); native::locale::loadOnce();
    assert(opens==1 && closes==1 && requested.find("ja.bin")!=std::string::npos);
    assert(std::strcmp(native::locale::label(fruit,"fallback"),text)==0);
    native::locale::attempted=false; missing=true;
    native::locale::loadOnce();
    assert(opens==3 && closes==2 && requested.find("en-US.bin")!=std::string::npos);
    native::locale::attempted=false; missing=false; std::memcpy(desiredCode,"pt-BR",6);
    native::locale::loadOnce();
    assert(requested.find("pt-BR.bin")!=std::string::npos);
    native::locale::attempted=false; settingsFail=true;
    native::locale::loadOnce();
    assert(requested.find("en-US.bin")!=std::string::npos);
    native::locale::attempted=false; native::locale::pack={}; invalidSize=true;
    native::locale::loadOnce();
    assert(std::strcmp(native::locale::label(fruit,"fallback"),"fallback")==0);
    if(argc==2) for(const auto* code:localization::locales) {
        std::ifstream file(std::string(argv[1])+"/"+code+".bin",std::ios::binary);
        assert(file);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
        assert(pack.load(bytes.data(),bytes.size()));
        assert(std::strcmp(pack.label(fruit,"missing"),"missing")!=0);
    }
}
