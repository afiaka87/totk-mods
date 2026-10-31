// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/native/Categories.cpp"
#include "../src/pure/SelectionState.hpp"
#include "../src/native/SlotObservation.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace material_columns;
namespace {
struct File { std::string name, text; };
std::vector<File> files;
unsigned nextEntry{}, opens{}, closes{};
bool missing{}, failRead{};
std::vector<config::Issue> issues;
void record(void*,unsigned,config::Issue issue) { issues.push_back(issue); }
bool parse(config::Registry& registry,const std::string& text,config::Pass pass,const char* language="en-US") {
    config::Parser parser(registry,native::categories::builtin,pass,language,record);
    std::size_t start=0;
    while(start<text.size()) {
        auto end=text.find('\n',start); if(end==text.npos) end=text.size();
        parser.line(std::string_view(text).substr(start,end-start)); start=end+1;
    }
    return parser.finish();
}
std::string category="[General]\nVersion=1\n[Materials]\nObj_RemoteBombSphere=test.bombs\n"
    "[Category.test.bombs]\nName.en-US=Remote bombs\nName.fr=Bombes\nName.fr-CA=Bombes CA\n";
void reset() {
    native::categories::registry={}; native::categories::attempted=false;
    nextEntry=opens=closes=0; failRead=missing=false;
}
}
namespace nn::fs {
Result OpenDirectory(DirectoryHandle*,const char* path,int mode) {
    assert(std::string(path)=="content:/MaterialColumns/Categories" && mode==2);
    nextEntry=0; return {!missing};
}
Result ReadDirectory(s64* count,DirectoryEntry* out,DirectoryHandle,s64 capacity) {
    assert(capacity==1); *count=nextEntry<files.size();
    if(*count) { std::strcpy(out->mName,files[nextEntry++].name.c_str()); }
    return {true};
}
void CloseDirectory(DirectoryHandle) {}
Result OpenFile(FileHandle* out,const char* path,int mode) {
    assert(mode==OpenMode_Read); ++opens;
    for(unsigned i=0;i<files.size();++i) if(std::string(path)=="content:/MaterialColumns/Categories/"+files[i].name) {
        out->index=i; return {true};
    }
    return {false};
}
Result GetFileSize(s64* out,FileHandle f) { *out=s64(files[f.index].text.size()); return {true}; }
Result ReadFile(FileHandle f,s64 offset,void* out,u64 bytes) {
    if(failRead) return {false};
    assert(offset+bytes<=files[f.index].text.size());
    std::memcpy(out,files[f.index].text.data()+offset,bytes); return {true};
}
void CloseFile(FileHandle) { ++closes; }
}
int main(int argc,char** argv) {
    config::Registry registry;
    assert(config::slice({},0).empty() && config::slice("abc",7).empty());
    assert(config::slice("abc",1,20)=="bc");
    assert(sizeof(registry)<=13*1024);
    assert(config::identifier("Obj_RemoteBombSphere",true));
    assert(!config::identifier("Remote bomb",true) && !config::identifier("../fruit"));
    assert(config::key("fruit",true)==identity("group:fruit"));
    assert(config::languageScore("pt","pt-BR")==2 && config::languageScore("zh-Hans","zh-Hant")==0);
    assert(config::languageScore("unknown","en-US")==-1);
    assert(parse(registry,category,config::Pass::Validate) && !registry.categories() && !registry.mappings());
    assert(parse(registry,category,config::Pass::Categories,"fr-CA"));
    assert(std::string(registry.label(config::key("test.bombs",true),"missing"))=="Bombes CA");
    assert(parse(registry,category,config::Pass::Materials));
    assert(registry.mapped(identity("Obj_RemoteBombSphere"))==config::key("test.bombs",true));
    assert(parse(registry,"[General]\nVersion=1\n[Materials]\nObj_RemoteBombSphere=fruit",config::Pass::Materials));
    assert(registry.mapped(identity("Obj_RemoteBombSphere"))==identity("group:fruit"));
    assert(issues.back()==config::Issue::Override);
    parse(registry,"[General]\nVersion=1\n[Materials]\nObj_RemoteBombSphere=typo",config::Pass::Materials);
    assert(registry.mapped(identity("Obj_RemoteBombSphere"))==identity("group:fruit"));
    assert(!parse(registry,"[General]\nVersion=2",config::Pass::Validate));
    assert(!parse(registry,"[General]\nVersion=1\nVersion=2",config::Pass::Validate));
    assert(!parse(registry,"[Materials]\nA=fruit",config::Pass::Validate));
    const auto before=registry.categories();
    parse(registry,"[General]\nVersion=1\n[Category.test.no-english]\nName.ja=Test",config::Pass::Categories);
    parse(registry,"[General]\nVersion=1\n[Category.fruit]\nName.en-US=Wrong",config::Pass::Categories);
    parse(registry,"[General]\nVersion=1\n[Category.test.long]\nName.en-US="+std::string(64,'a'),config::Pass::Categories);
    assert(registry.categories()==before);
    for(unsigned i=0;i<config::maxCategories;++i) registry.category(i+1,"Category");
    assert(registry.categories()==config::maxCategories && !registry.category(9999,"Overflow"));
    for(unsigned i=0;i<config::maxMappings;++i) registry.map(i+1,1);
    assert(registry.mappings()==config::maxMappings && !registry.map(9999,1));
    assert(registry.map(1,2));

    files={{"zz-override.ini","[General]\nVersion=1\n[Materials]\nObj_RemoteBombCube=test.bombs\nSorting_Probe=eyeballs\n"},
           {"bombs.ini","\xef\xbb\xbf"+category+"[Materials]\nSorting_Probe=fruit\n"},
           {"bad.ini","[General]\nVersion=99\n[Materials]\nUnknown=fruit\n"},
           {"ignored.txt",category}};
    reset(); native::categories::loadOnce("fr-CA");
    assert(opens==closes && native::categories::registry.categories()==1 && native::categories::registry.mappings()==3);
    assert(native::categories::registry.mapped(identity("Sorting_Probe"))==catalog::eyes);
    const auto reads=opens; native::categories::loadOnce("en-US"); assert(opens==reads);
    CatalogMatch match{};
    assert(native::categories::lookup(10,match) && match.item_rank==10 && match.group==catalog::fruit);
    const auto bomb=identity("Obj_RemoteBombSphere");
    std::array<unsigned char,80> slot{};
    const char* actorName="Obj_RemoteBombSphere"; std::int32_t quantity=3;
    std::memcpy(slot.data(),&actorName,sizeof(actorName)); slot[9]=1;
    std::memcpy(slot.data()+12,&quantity,sizeof(quantity));
    const auto observation=native::observeSlot(slot.data());
    assert(observation.eligible && observation.item==bomb && observation.quantity==3);
    assert(native::categories::lookup(bomb,match) && match.group==config::key("test.bombs",true));
    assert(!native::categories::lookup(identity("Unassigned"),match));
    SelectionState<8,96> state;
    const InventoryEntry inventory[]{{10,3,false,true,1},{bomb,7,false,true,20},
        {identity("Obj_RemoteBombCube"),9,false,true,5},{identity("Unassigned"),12,false,true,0}};
    assert(state.rebuild(inventory,4,native::categories::lookup,native::fallback(false),native::fallback(true)));
    assert(state.groupId(0)==config::key("test.bombs",true));
    assert(state.selectNative(9) && state.selected()->native_slot==9);
    const auto saved=state.memory();
    SelectionState<8,96> restored;
    assert(restored.load(saved));
    assert(restored.rebuild(inventory,4,native::categories::lookup,native::fallback(false),native::fallback(true)));
    assert(restored.selected()->native_slot==9);
    native::categories::registry.category(config::key("test.bombs",true),"Renamed caption");
    assert(restored.rebuild(inventory,4,native::categories::lookup,native::fallback(false),native::fallback(true)));
    assert(restored.selected()->native_slot==9);
    reset(); missing=true; native::categories::loadOnce("en-US"); assert(!opens);
    reset(); failRead=true; native::categories::loadOnce("en-US"); assert(!native::categories::registry.mappings());
    reset(); files.assign(33,{"a.ini",category}); native::categories::loadOnce("en-US"); assert(!opens);
    reset(); files={{std::string(60,'a')+".ini",category}}; native::categories::loadOnce("en-US"); assert(!opens);
    reset(); files={{"long.ini","[General]\nVersion=1\n"+std::string(700,'x')+"\n[Materials]\nA=fruit"}};
    native::categories::loadOnce("en-US"); assert(native::categories::registry.mappings()==1);
    reset(); files={{"huge.ini",std::string(65537,'x')}}; native::categories::loadOnce("en-US");
    assert(!native::categories::registry.mappings() && opens==closes);
    if(argc>1) {
        files.clear();
        for(int i=1;i<argc;++i) {
            std::ifstream in(argv[i],std::ios::binary); assert(in);
            files.push_back({std::to_string(i)+".ini",{std::istreambuf_iterator<char>(in),{}}});
        }
        for(const char* language:localization::locales) {
            reset(); native::categories::loadOnce(language);
            assert(native::categories::registry.mappings()==78 && native::categories::registry.categories()==4);
            assert(native::categories::registry.mapped(identity("Item_Enemy_124_Senior"))==identity("group:wings"));
            assert(native::categories::registry.mapped(identity("Item_Ore_B_Star"))==identity("group:star-fragments"));
            assert(native::categories::registry.mapped(identity("SpObj_SpringPiston_Capsule_Gold"))==identity("group:zonai-devices"));
            assert(native::categories::registry.mapped(identity("Animal_Kokko_A"))==identity("group:special-materials"));
            assert(native::categories::registry.mapped(identity("Item_Wax_01"))==identity("group:invisible-attachments.attachments"));
            assert(native::categories::registry.mapped(identity("Item_Example_Mooncap"))==identity("group:mushrooms"));
            assert(std::strcmp(native::categories::registry.label(config::key("botw-abilities.remote-bombs",true),"missing"),"missing"));
        }
    }
}
