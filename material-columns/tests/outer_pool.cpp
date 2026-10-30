// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define MATERIAL_COLUMNS_PANEL_HOST_TEST
#include <cassert>
#include <cstdint>
#include <array>
#include <cstdio>
std::uintptr_t initializePage(std::uintptr_t, const std::uintptr_t*);
std::uintptr_t bindNative(std::uintptr_t, std::uintptr_t, unsigned, std::uintptr_t);
struct PoolTestHook {
    static std::uintptr_t Orig(std::uintptr_t page, const std::uintptr_t* args) { return initializePage(page,args); }
    static std::uintptr_t Orig(std::uintptr_t element, std::uintptr_t slot, unsigned index, std::uintptr_t request) { return bindNative(element,slot,index,request); }
    template<class... Args> static std::uintptr_t Orig(Args...) { return 0; }
    static void InstallAtOffset(std::uintptr_t) {}
};
#define HOOK_DEFINE_TRAMPOLINE(Name) struct Name : PoolTestHook
#include "../src/native/ColumnPanel.cpp"
using namespace material_columns::native;
namespace {
template<class T> std::uintptr_t address(T& v) { return reinterpret_cast<std::uintptr_t>(v.data()); }
std::array<std::array<std::byte,512>,6> poolPanes{}, poolLayouts{}, poolButtons{}, poolSlots{};
unsigned allocated{}, bindings{}, moves{}, contentUpdates{};
int quantity=999;
std::array<bool,6> iconReady{};
std::array<bool,6> quantityVisible{}, highlighted{};
std::array<bool,6> slotShown{};
bool missingUpper{};
void nativePageLifecycle(std::uintptr_t page) {
    // onInactive_ resolves the current slot through page+72 even when the
    // element's old item is departing. It clears icon and quantity state.
    for (unsigned row=1;row<3;++row) {
        const auto slot=field<std::uintptr_t>(field<std::uintptr_t>(page,72),row*8);
        const auto button=field<std::uintptr_t>(field<std::uintptr_t>(page,40),row*8);
        for(unsigned i=0;i<6;++i) {
            if(slot==address(poolSlots[i])) iconReady[i]=quantityVisible[i]=false;
            // Page visibility activates its generic buttons; cursor selection
            // can then animate the same Select/Unselect tracks as the slot.
            if(button==address(poolButtons[i])) highlighted[i]=true;
        }
    }
}
}
std::uintptr_t initializePage(std::uintptr_t page, const std::uintptr_t* args) {
    field<std::uintptr_t>(page,8)=args[1]; field<unsigned>(page,16)=3;
    field<std::uintptr_t>(page,24)=args[3]; field<unsigned>(page,32)=3;
    field<std::uintptr_t>(page,40)=args[4]; field<std::uintptr_t>(page,48)=args[2];
    field<unsigned>(page,64)=3;
    for(unsigned row=0;row<3;++row) {
        const auto pane=field<std::uintptr_t>(args[3],row*8);
        const auto layout=field<std::uintptr_t>(pane,232);
        if(!layout) continue; // Native E744FC leaves skipped array entries uninitialized.
        assert(allocated<6 && pane==address(poolPanes[allocated]));
        const auto slot=address(poolSlots[allocated]);
        field<std::uintptr_t>(slot,32)=layout;
        field<std::uintptr_t>(slot,168)=address(poolButtons[allocated]);
        field<std::uintptr_t>(field<std::uintptr_t>(page,72),row*8)=slot;
        ++allocated;
    }
    return 0;
}
std::uintptr_t bindNative(std::uintptr_t element,std::uintptr_t slot,unsigned index,std::uintptr_t ptr) {
    const auto& request=*reinterpret_cast<const BindingRequest*>(ptr);
    assert(index==static_cast<unsigned>(request.logical+1));
    assert(element==1000+static_cast<unsigned>(request.logical));
    assert(request.page && request.pane && request.offset==0);
    // Native setSlot resets pending/completed icon work on every execution.
    // The retained presentation must prevent that reset on repeated frames.
    std::array<std::uint8_t,96> parameters{};
    char actor[64]; std::snprintf(actor,sizeof(actor),"Item_%u",index);
    const char* name=actor;
    std::memcpy(parameters.data(),&name,sizeof(name));
    std::memcpy(parameters.data()+12,&quantity,sizeof(quantity));
    parameters[9]=1;parameters[10]=static_cast<std::uint8_t>(bindings); // Native padding varies.
    if(!retainPooledSlot(slot,parameters.data())) {
        ++contentUpdates;
        for(unsigned i=0;i<6;++i) if(address(poolSlots[i])==slot) {
            iconReady[i]=false;quantityVisible[i]=true;
        }
    }
    ++bindings; return 1;
}
namespace material_columns::native {
std::uintptr_t updateMaterialShortcut(std::uintptr_t child, NativeUpdate original) { return original(child); }
void forgetMaterialScreen(std::uintptr_t) {}
namespace {
void remove(std::uintptr_t parent,std::uintptr_t pane) { assert(field<std::uintptr_t>(pane,24)==parent);field<std::uintptr_t>(pane,24)=0; }
void append(std::uintptr_t parent,std::uintptr_t pane) { assert(!field<std::uintptr_t>(pane,24));field<std::uintptr_t>(pane,24)=parent;++moves; }
void show(std::uintptr_t slot,int visible) {
    for(unsigned i=0;i<6;++i) if(address(poolSlots[i])==slot) slotShown[i]=visible!=0;
}
void select(std::uintptr_t,bool selected,bool animate) { assert(!selected && !animate); }
std::uintptr_t resolvePanelTestFunction(std::uintptr_t offset) {
    switch(offset) {
    case 0xd84d30:return reinterpret_cast<std::uintptr_t>(&remove);
    case 0x700480:return reinterpret_cast<std::uintptr_t>(&append);
    case 0x11f6dcc:return reinterpret_cast<std::uintptr_t>(&show);
    case 0x1a584ec:return reinterpret_cast<std::uintptr_t>(&select);
    default:assert(false);return 0;
    }
}
bool bindItem(void*,std::uint32_t logical,std::uint32_t& native,bool& selected) {
    native=logical;selected=false;return !(missingUpper && logical%3==1);
}
}
}
int main() {
    std::array<std::byte,512> control{}, owner{}, stock{}, stockControl{}, park{};
    std::array<std::array<std::byte,512>,11> pages{}, pagePanes{};
    std::array<std::array<std::array<std::byte,512>,3>,11> containers{}, layouts{}, buttons{};
    std::array<std::array<std::uintptr_t,3>,11> paneRefs{}, buttonRefs{}, slotRefs{};
    std::array<std::uintptr_t,11> pageRefs{};
    std::array<std::uintptr_t,33> elements{};
    auto& panel=panels[0];panel={}; panel.owner=address(owner);panel.controller=address(control);panel.count=11;
    panel.visible=panel.ready=true;panel.bind=bindItem;
    field<std::uintptr_t>(panel.owner,424)=address(stock);
    field<std::uintptr_t>(address(stock),80)=address(stockControl);
    field<unsigned>(address(stockControl),136)=100;
    field<unsigned>(panel.controller,88)=11;
    field<std::uintptr_t>(panel.controller,96)=address(pageRefs);
    field<unsigned>(panel.controller,104)=33;
    field<std::uintptr_t>(panel.controller,112)=address(elements);
    for(unsigned i=0;i<33;++i) elements[i]=1000+i;
    for(unsigned i=0;i<6;++i) {
        panel.outer[i].pane=address(poolPanes[i]);panel.outerPark=address(park);
        field<std::uintptr_t>(address(poolPanes[i]),24)=address(park);
        field<std::uintptr_t>(address(poolPanes[i]),232)=address(poolLayouts[i]);
        field<std::uintptr_t>(address(poolButtons[i]),32)=address(poolLayouts[i]);
    }
    for(unsigned i=0;i<11;++i) {
        pageRefs[i]=address(pages[i]);
        field<int>(pageRefs[i],56)=i;
        field<std::uintptr_t>(pageRefs[i],72)=address(slotRefs[i]);
        for(unsigned row=0;row<3;++row) {
            paneRefs[i][row]=address(containers[i][row]);buttonRefs[i][row]=address(buttons[i][row]);
            field<std::uintptr_t>(paneRefs[i][row],232)=address(layouts[i][row]);
            field<std::uintptr_t>(address(layouts[i][row]),24)=paneRefs[i][row];
            field<std::uintptr_t>(buttonRefs[i][row],32)=address(layouts[i][row]);
            slotRefs[i][row]=0xdeadbeef;
        }
        const auto original=paneRefs[i];
        const std::uintptr_t args[]={0,panel.controller,address(pagePanes[i]),address(paneRefs[i]),address(buttonRefs[i])};
        InitializeColumnPage::Callback(pageRefs[i],args);
        assert(paneRefs[i]==original);
        for(unsigned row=0;row<3;++row) assert(field<std::uintptr_t>(paneRefs[i][row],232)==address(layouts[i][row]));
    }
    assert(allocated==6 && panel.pagesCreated==11 && validateColumnRows(panel));
    assert(prepareOuterTiles(panel));
    for(const auto& slots:slotRefs) for(auto slot:slots) assert(!slot);
    const auto view=[&](float center,int firstGroup=0) {
        panel.count=firstGroup+11;
        for(unsigned i=0;i<11;++i) {
            field<int>(pageRefs[i],56)=firstGroup+i;
            field<float>(address(pagePanes[i]),48)=(float(i)-center)*170.f;
        }
        placeOuterTiles(panel);
        const int nearest=static_cast<int>(std::round(center));
        unsigned attached{};
        for(unsigned i=0;i<11;++i) for(unsigned row=1;row<3;++row) {
            if(std::abs(int(i)-nearest)<=1) ++attached;
        }
        const auto before=bindings;bindOuterTiles(panel);
        assert(bindings-before==attached && attached<=6);
        iconReady.fill(true); // The native asynchronous loader completes.
        const auto priorUpdates=contentUpdates;
        quantityVisible.fill(true);highlighted.fill(false);
        for(unsigned frame=0;frame<60;++frame) {
            for(const auto page:pageRefs) nativePageLifecycle(page);
            bindOuterTiles(panel);
        }
        assert(contentUpdates==priorUpdates);
        for(const auto ready:iconReady) assert(ready); // No frame can blank a settled icon.
        for(const auto visible:quantityVisible) assert(visible);
        for(const auto selected:highlighted) assert(!selected);
        for(unsigned i=0;i<11;++i) for(unsigned row=1;row<3;++row) {
            assert(!slotRefs[i][row]);
            assert(buttonRefs[i][row]==address(buttons[i][row]));
        }
        for(const auto& tile:panel.outer) {
            assert(!(field<unsigned>(tile.parts,64)&0x10));
            assert(field<int>(tile.parts,68)==-1);
        }
        ++quantity;bindOuterTiles(panel);
        assert(contentUpdates==priorUpdates+attached); // Live quantities still refresh.
        bindOuterTiles(panel);assert(contentUpdates==priorUpdates+attached);
    };
    for(float center:{0.f,1.f,5.f,4.f,10.f,3.49f,3.51f}) view(center);
    // Recycled physical pages now describe completely different logical categories.
    field<unsigned>(panel.controller,104)=100;
    std::array<std::uintptr_t,100> moreElements{};
    for(unsigned i=0;i<100;++i) moreElements[i]=1000+i;
    field<std::uintptr_t>(panel.controller,112)=address(moreElements);
    view(5,20);view(4,20);view(5,20);
    missingUpper=true;bindOuterTiles(panel);
    for(unsigned i=0;i<6;++i) assert(slotShown[i]==bool(i%2));
    missingUpper=false;bindOuterTiles(panel);
    for(const auto shown:slotShown) assert(shown);
    parkOuterTiles(panel);
    for(const auto& tile:panel.outer) assert(!tile.page && field<std::uintptr_t>(tile.pane,24)==address(park));
    const auto parkedMoves=moves;parkOuterTiles(panel);assert(moves==parkedMoves);
    for(const auto& slots:slotRefs) for(auto slot:slots) assert(!slot);
    parkOuterTiles(panel,true);
    for(unsigned i=0;i<11;++i) for(unsigned row=0;row<3;++row) {
        const auto expected=i<3 && row ? address(poolSlots[i*2+row-1]) : 0;
        assert(slotRefs[i][row]==expected); // Exactly one owner per pooled slot at destruction.
        assert(buttonRefs[i][row]==address(buttons[i][row]));
    }
}
