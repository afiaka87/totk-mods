// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define MATERIAL_COLUMNS_INVENTORY_HOST_TEST
#include <cassert>
struct InventoryHook {
    static unsigned long long Orig(unsigned long long, unsigned long long, const void*) { return 99; }
    static void InstallAtOffset(unsigned long long) {}
};
#define HOOK_DEFINE_TRAMPOLINE(Name) struct Name : InventoryHook
#include "../src/native/NativeInventory.cpp"
using namespace material_columns;
using namespace material_columns::native;
namespace material_columns::native {
bool retainPooledSlot(std::uintptr_t, const void*) { return false; }
namespace {
bool tablePresent=true, counterPresent=true;
unsigned reads{}, hashes{}, tables{};
std::uintptr_t manager=1234;
bool getTable(std::uintptr_t mgr, void* handle, std::uint32_t key) {
    assert(mgr==manager && key==0x5a1a0a90); ++tables;
    static_cast<std::uint64_t*>(handle)[0]=5678;
    return tablePresent;
}
std::uint32_t hashActor(const char* actor, std::uint32_t seed) {
    assert(std::strcmp(actor,"Item_Fruit_A")==0 && seed==0); ++hashes; return 4321;
}
bool getCount(std::uintptr_t mgr,std::uint32_t* count,const void* handle,std::uint32_t key) {
    assert(mgr==manager && *static_cast<const std::uint64_t*>(handle)==5678 && key==4321);
    ++reads; *count=counterPresent ? 123 : 999; return counterPresent;
}
std::uint32_t physical(std::uintptr_t pouch,std::uint32_t index,std::uint32_t category,std::uint32_t* out) {
    assert(pouch==2468 && index==0 && category==5); *out=7; return 0;
}
std::uintptr_t binder(std::uintptr_t,std::uintptr_t token,std::uint32_t index,std::uintptr_t request) {
    assert(index==1 && reinterpret_cast<BindingRequest*>(request)->logical==1);
    std::array<std::uint8_t,96> params{};
    const char* actor="Item_Fruit_A"; std::memcpy(params.data(),&actor,8);
    params[9]=1;params[12]=4;
    return MaterialSlotCapture::Callback(0,token,params.data());
}
std::uintptr_t resolveInventoryTestFunction(std::uintptr_t offset) {
    switch(offset) {
    case 0xddbb04:return reinterpret_cast<std::uintptr_t>(&getTable);
    case 0x89011c:return reinterpret_cast<std::uintptr_t>(&hashActor);
    case 0x170f1cc:return reinterpret_cast<std::uintptr_t>(&getCount);
    case 0x19aefb0:return reinterpret_cast<std::uintptr_t>(&physical);
    case 0x1a7f090:return reinterpret_cast<std::uintptr_t>(&binder);
    default:assert(false);return 0;
    }
}
}
}
int main() {
    std::array<std::uint64_t,1024> globals{};
    base=reinterpret_cast<std::uintptr_t>(globals.data())-0x462e3d8;
    std::uintptr_t mgr=manager,pouch=2468;
    globals[0]=reinterpret_cast<std::uintptr_t>(&mgr);
    globals[(0x4630020-0x462e3d8)/8]=reinterpret_cast<std::uintptr_t>(&pouch);
    std::array<std::uint64_t,64> screen{},stock{},control{},page{};
    auto address=[](auto& a){return reinterpret_cast<std::uintptr_t>(a.data());};
    auto set=[](auto& a,unsigned offset,auto value){std::memcpy(reinterpret_cast<char*>(a.data())+offset,&value,sizeof(value));};
    auto pageAddress=address(page);
    set(screen,440,std::uint32_t{3});set(screen,424,address(stock));
    set(stock,80,address(control));set(stock,154,std::uint8_t{1});
    set(control,96,reinterpret_cast<std::uintptr_t>(&pageAddress));set(control,88,std::int32_t{1});
    set(control,136,std::int32_t{2});set(page,8,address(control));
    installed=true;
    InventoryEntry entry{};std::size_t count{};
    assert(readInventory(address(screen),&entry,1,count));
    assert(count==1 && entry.usage==123 && entry.eligible && entry.device && reads==1 && hashes==1);
    counterPresent=false;
    assert(readInventory(address(screen),&entry,1,count) && entry.usage==0);
    tablePresent=false;
    assert(readInventory(address(screen),&entry,1,count) && entry.usage==0 && reads==2);
    assert(tables==3);
    set(screen,440,std::uint32_t{0});
    assert(!readInventory(address(screen),&entry,1,count) && tables==3); // Weapon menu never reads counters.
    Probe unrelated;
    assert(MaterialSlotCapture::Callback(0,reinterpret_cast<std::uintptr_t>(&unrelated),nullptr)==99); // Unrelated native calls forward.
}
