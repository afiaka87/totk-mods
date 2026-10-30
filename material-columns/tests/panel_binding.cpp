// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define MATERIAL_COLUMNS_PANEL_HOST_TEST
#include <cassert>
#include <cstdint>
#include <cstring>
std::uintptr_t forwardBinding(std::uintptr_t, std::uintptr_t, std::uint32_t, std::uintptr_t);
std::uintptr_t forwardCalculation(std::uintptr_t, std::uintptr_t, bool);
bool copyingRow = false;
std::uintptr_t forwardRowCopy(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
struct PanelTestHook {
    template<class... Args> static std::uintptr_t Orig(Args... args) {
        if constexpr (sizeof...(Args) == 4) {
            if (copyingRow) return forwardRowCopy(args...);
            return forwardBinding(args...);
        }
        if constexpr (sizeof...(Args) == 3) return forwardCalculation(args...);
        return 0;
    }
    static void InstallAtOffset(std::uintptr_t) {}
};
#define HOOK_DEFINE_TRAMPOLINE(Name) struct Name : PanelTestHook
#include "../src/native/ColumnPanel.cpp"
using namespace material_columns::native;
namespace {
std::uintptr_t expectedRoot{}, expectedRequest{}, expectedSlot = 123;
unsigned captions{}, binds{}, clears{}, outlines{}, reparents{}, invalidations{};
std::uintptr_t expectedTemplate{}, expectedClone{};
unsigned copiedControllers{};
bool hasItem = true;
std::uintptr_t testLayout{}, stockPanePtr{}, columnPanePtr{}, guidePtr{};
std::array<std::uintptr_t, 3> rowPanes{};
}
std::uintptr_t forwardBinding(std::uintptr_t, std::uintptr_t slot, std::uint32_t logical, std::uintptr_t request) {
    assert(slot == expectedSlot && logical == 8 && request == expectedRequest);
    ++binds;
    return 42;
}
std::uintptr_t forwardCalculation(std::uintptr_t layout, std::uintptr_t, bool) {
    if (layout == testLayout) {
        assert(!(field<unsigned char>(stockPanePtr, 88) & 1));
        assert(field<unsigned char>(columnPanePtr, 88) & 1);
        assert(field<float>(guidePtr, 52) == -470.f);
        for (unsigned row = 0; row < 3; ++row) {
            assert(bool(field<unsigned char>(rowPanes[row], 88) & 1) == (row == 0));
            assert(field<unsigned char>(rowPanes[row], 89) == 255);
            assert(field<float>(rowPanes[row], 52) == (row == 0 ? 0.f : row == 1 ? 180.f : -180.f));
        }
    }
    return 71;
}
std::uintptr_t forwardRowCopy(std::uintptr_t, std::uintptr_t, std::uintptr_t source, std::uintptr_t destination) {
    assert(source == expectedTemplate && destination == expectedClone);
    // The native copier selects controllers by exact source layout identity.
    // The inert center has none; the real outer template includes icon controllers.
    copiedControllers += field<unsigned>(source, 240);
    return 91;
}
namespace material_columns::native {
std::uintptr_t updateMaterialShortcut(std::uintptr_t child, NativeUpdate original) { return original(child); }
void forgetMaterialScreen(std::uintptr_t) {}
namespace {
std::uintptr_t lookupPane(std::uintptr_t root, const char* name, bool recursive) {
    // The old callback would read beyond BindingRequest and pass address 1 here.
    assert(root == expectedRoot && recursive && std::strcmp(name, "T_MCGroup_00") == 0);
    return root;
}
void setText(std::uintptr_t pane, const char16_t* text, unsigned length) {
    assert(pane == expectedRoot && length == 5 && text[0] == u'F');
    std::memcpy(reinterpret_cast<void*>(field<std::uintptr_t>(pane, 216)), text, length * sizeof(char16_t));
    field<std::uint16_t>(pane, 274) = static_cast<std::uint16_t>(length);
    ++captions;
}
void setSlotVisible(std::uintptr_t slot, int visible) {
    assert(slot == expectedSlot);
    if (!visible) ++clears;
}
void setSelected(std::uintptr_t slot, bool selected, bool animate) {
    assert(slot == expectedSlot && selected && animate);
    ++outlines;
}
std::uintptr_t lookupLayoutPane(std::uintptr_t, const char* name) {
    return std::strcmp(name, "N_Guide_00") == 0 ? guidePtr : 0;
}
void removeChild(std::uintptr_t parent, std::uintptr_t pane) {
    assert(field<std::uintptr_t>(pane,24)==parent); field<std::uintptr_t>(pane,24)=0;
}
void appendChild(std::uintptr_t parent, std::uintptr_t pane) {
    assert(field<std::uintptr_t>(pane,24)==0); field<std::uintptr_t>(pane,24)=parent; ++reparents;
}
void invalidate(std::uintptr_t) { ++invalidations; }
std::uintptr_t resolvePanelTestFunction(std::uintptr_t offset) {
    switch (offset) {
    case 0xd84d30: return reinterpret_cast<std::uintptr_t>(&removeChild);
    case 0x700480: return reinterpret_cast<std::uintptr_t>(&appendChild);
    case 0x1a36180: return reinterpret_cast<std::uintptr_t>(&invalidate);
    case 0x7012ec: return reinterpret_cast<std::uintptr_t>(&lookupLayoutPane);
    case 0x470f8: return reinterpret_cast<std::uintptr_t>(&lookupPane);
    case 0x22acb04: return reinterpret_cast<std::uintptr_t>(&setText);
    case 0x11f6dcc: return reinterpret_cast<std::uintptr_t>(&setSlotVisible);
    case 0x1a584ec: return reinterpret_cast<std::uintptr_t>(&setSelected);
    default: assert(false); return 0;
    }
}
bool bindItem(void*, std::uint32_t logical, std::uint32_t& native, bool& selected) {
    assert(logical >= 3 && logical <= 5); native = logical + 4; selected = logical == 3; return hasItem;
}
ColumnAppearance describeGroup(std::size_t group) { assert(group == 1); return {"Fruit", false, true}; }
}
}
int main() {
    alignas(8) std::array<std::byte, 512> screen{}, stock{}, control{}, page{}, root{};
    const auto address = [](auto& value) { return reinterpret_cast<std::uintptr_t>(value.data()); };
    const auto screenPtr = address(screen), controlPtr = address(control), pagePtr = address(page);
    expectedRoot = address(root);
    char16_t captionBuffer[128]{};
    field<std::uintptr_t>(expectedRoot, 216) = reinterpret_cast<std::uintptr_t>(captionBuffer);
    field<std::uintptr_t>(screenPtr, 424) = address(stock);
    field<std::uintptr_t>(address(stock), 80) = controlPtr;
    field<int>(controlPtr, 136) = 20;
    field<std::uintptr_t>(pagePtr, 8) = controlPtr;
    field<std::uintptr_t>(pagePtr, 48) = expectedRoot;
    // Poison the offset the old code incorrectly interpreted as a page root.
    struct RequestFrame { BindingRequest request; std::uintptr_t padding[3]; } frame{{3, 0, controlPtr, pagePtr, 0}, {0, 0, 1}};
    static_assert(offsetof(RequestFrame, padding) + 16 == 48);
    expectedRequest = reinterpret_cast<std::uintptr_t>(&frame.request);
    auto& panel = panels[0];
    panel.owner = screenPtr; panel.controller = controlPtr; panel.count = 2;
    panel.ready = panel.visible = true; panel.bind = bindItem; panel.describe = describeGroup;
    assert(BindShortcutItem::Callback(0, expectedSlot, 3, expectedRequest) == 42);
    assert(captions == 1 && binds == 1 && outlines == 1 && clears == 0);
    // Empty rows clear normally; missing/mismatched pages never reach pane lookup.
    hasItem = false;
    assert(BindShortcutItem::Callback(0, expectedSlot, 3, expectedRequest) == 0);
    frame.request.page = 0;
    assert(BindShortcutItem::Callback(0, expectedSlot, 3, expectedRequest) == 0);
    frame.request.page = pagePtr;
    field<std::uintptr_t>(pagePtr, 8) = controlPtr + 1;
    assert(BindShortcutItem::Callback(0, expectedSlot, 3, expectedRequest) == 0);
    assert(captions == 1 && binds == 1 && clears == 3);
    // An unchanged caption must not restart native glyph preparation every frame.
    setCaption(panel, pagePtr, 1);
    assert(captions == 1);
    field<std::uint8_t>(expectedRoot, 360) = 2;
    captionBuffer[0] = u'X';
    setCaption(panel, pagePtr, 1);
    assert(captions == 2 && captionBuffer[0] == u'F');
    assert(field<std::uint8_t>(expectedRoot, 360) == 0);
    field<std::uint8_t>(expectedRoot, 360) = 1;
    captionBuffer[0] = u'X';
    setCaption(panel, pagePtr, 1);
    assert(field<std::uint8_t>(expectedRoot, 360) == 1);
    // Caption visibility follows the actual pooled window, with only a slight neighbor fade.
    panel.outerReady=true;panel.outer[0].page=pagePtr;
    for(float x:{0.f,170.f,-170.f,255.f,340.f}) {
        field<float>(expectedRoot,48)=x;setCaption(panel,pagePtr,1);
        assert(field<std::uint8_t>(expectedRoot,89)==static_cast<std::uint8_t>(255.f*material_columns::columnLabelOpacity(x)));
        assert(bool(field<std::uint8_t>(expectedRoot,88)&1)==(std::abs(x)<255.f));
    }
    field<float>(expectedRoot,48)=0;panel.outer[0].page=0;setCaption(panel,pagePtr,1);
    assert(!(field<std::uint8_t>(expectedRoot,88)&1));
    panel.outerReady=false;
    // Native animation has just restored stock visibility/spacing and hidden the rows.
    // The calculate boundary must fix all of them BEFORE native matrix calculation.
    alignas(8) std::array<std::byte, 512> parent{}, privatePane{}, stockPane{}, guide{}, r0{}, r1{}, r2{};
    testLayout = address(parent); columnPanePtr = address(privatePane);
    stockPanePtr = address(stockPane); guidePtr = address(guide);
    rowPanes = {address(r0), address(r1), address(r2)};
    std::array<std::uintptr_t, 3> slots{expectedSlot, expectedSlot, expectedSlot};
    std::array<std::uintptr_t, 1> pageList{pagePtr};
    panel.pane = columnPanePtr; panel.stockPane = stockPanePtr;
    field<std::uintptr_t>(screenPtr, 40) = testLayout;
    field<std::uintptr_t>(controlPtr, 96) = reinterpret_cast<std::uintptr_t>(pageList.data());
    field<int>(controlPtr, 88) = 1;
    field<int>(pagePtr, 56) = 1;
    field<int>(pagePtr, 16) = field<int>(pagePtr, 64) = 3;
    field<std::uintptr_t>(pagePtr, 24) = reinterpret_cast<std::uintptr_t>(rowPanes.data());
    field<std::uintptr_t>(pagePtr, 72) = reinterpret_cast<std::uintptr_t>(slots.data());
    field<unsigned char>(stockPanePtr, 88) = 1;
    field<float>(guidePtr, 52) = -200.f;
    hasItem = true;
    assert(CalculateColumnLayoutEx::Callback(testLayout, 0, false) == 71);
    panel.visible = false;
    field<float>(guidePtr, 52) = -200.f;
    // Inactive/foreign layouts must not run presentation overrides.
    assert(CalculateColumnLayoutEx::Callback(testLayout + 1, 0, false) == 71);
    assert(field<float>(guidePtr, 52) == -200.f);
    // Real row-copy hook: native sends the empty center template for ALL rows.
    // Page zero retains heterogeneous templates; later pages need matching sources.
    std::array<std::array<std::byte, 512>, 3> templates{}, clones{};
    std::array<std::uintptr_t, 3> templateRows{};
    for (unsigned row = 0; row < 3; ++row) {
        templateRows[row] = address(templates[row]);
        char name[] = "Pa_SlideItem_00"; name[14] = static_cast<char>('0' + row);
        std::strcpy(reinterpret_cast<char*>(templateRows[row] + 176), name);
        std::strcpy(reinterpret_cast<char*>(address(clones[row]) + 176), name);
        field<unsigned>(templateRows[row], 240) = row ? 3 : 0;
    }
    field<std::uintptr_t>(pagePtr, 24) = reinterpret_cast<std::uintptr_t>(templateRows.data());
    copyingRow = true;
    for (unsigned copy = 0; copy < 10; ++copy) for (unsigned row = 0; row < 3; ++row) {
        expectedTemplate = templateRows[row]; expectedClone = address(clones[row]);
        assert(CopyColumnRowLayout::Callback(controlPtr, 0, templateRows[0], expectedClone) == 91);
    }
    assert(copiedControllers == 60 && panel.rowTemplateCopies == 20);
    // Stock controllers retain the source the game supplies.
    expectedTemplate = templateRows[0]; expectedClone = address(clones[2]);
    assert(CopyColumnRowLayout::Callback(controlPtr + 1, 0, expectedTemplate, expectedClone) == 91);
    assert(copiedControllers == 60 && panel.rowTemplateCopies == 20);
    // Validate the actual native handoff before invoking the unguarded child initializer.
    std::array<std::array<std::byte, 256>, 3> rowControls{}, rowSlots{};
    std::array<std::uintptr_t, 3> rowControlRefs{}, rowSlotRefs{};
    for (unsigned row = 0; row < 3; ++row) {
        rowControlRefs[row] = address(rowControls[row]);
        rowSlotRefs[row] = row ? address(rowSlots[row]) : 0;
        field<std::uintptr_t>(rowControlRefs[row],32) = templateRows[row];
        if (row) {
            field<std::uintptr_t>(rowSlotRefs[row],32) = templateRows[row];
            panel.outer[row-1].slot = rowSlotRefs[row];
            panel.outer[row-1].pane = templateRows[row];
            field<std::uintptr_t>(templateRows[row],232) = templateRows[row];
        }
    }
    field<unsigned>(pagePtr,32)=3;
    field<std::uintptr_t>(pagePtr,40)=reinterpret_cast<std::uintptr_t>(rowControlRefs.data());
    field<std::uintptr_t>(pagePtr,72)=reinterpret_cast<std::uintptr_t>(rowSlotRefs.data());
    assert(validateColumnRows(panel));
    rowControlRefs[1]=0; assert(!validateColumnRows(panel));
    rowControlRefs[1]=address(rowControls[1]);
    field<std::uintptr_t>(rowSlotRefs[1],32)=templateRows[0]; assert(!validateColumnRows(panel));
    field<std::uintptr_t>(rowSlotRefs[1],32)=templateRows[1];
    field<unsigned>(pagePtr,32)=0; assert(!validateColumnRows(panel));
    field<unsigned>(pagePtr,32)=3;
    assert(validateColumnRows(panel));
    field<std::uintptr_t>(pagePtr,72)=reinterpret_cast<std::uintptr_t>(slots.data());
    copyingRow = false;
    field<std::uintptr_t>(pagePtr, 24) = reinterpret_cast<std::uintptr_t>(rowPanes.data());
    // Shared background SRT must sample the same world point, while the independent
    // gradient transform remains byte-identical across moving horizontal positions.
    alignas(8) std::array<std::byte,512> mask{}, capture{}, material{}, storage{};
    const auto m=address(mask), c=address(capture), mat=address(material), st=address(storage);
    field<std::uintptr_t>(m,216)=mat; field<unsigned>(mat,16)=0xau; field<std::uintptr_t>(mat,24)=st;
    field<float>(m,80)=170; field<float>(m,84)=72;
    field<float>(c,80)=680; field<float>(c,84)=72; field<float>(c,52)=244;
    for (unsigned j=52;j<72;++j) storage[j]=std::byte(j);
    const auto originalStorage=storage;
    for (float x : {-255.f,-170.f,0.f,170.f,255.f}) {
        field<float>(m,48)=x; field<float>(m,52)=244; sampleBand(m,c);
        for (float u : {0.f,.5f,1.f}) for (float v : {0.f,.5f,1.f}) {
            const float su=field<float>(st,44)*u+field<float>(st,32)+.5f-field<float>(st,44)*.5f;
            const float sv=-field<float>(st,48)*v+.5f-field<float>(st,36)+field<float>(st,48)*.5f;
            assert(std::abs((su-.5f)*680-(x+(u-.5f)*170))<.001f);
            assert(std::abs((sv-.5f)*72-(.5f-v)*72)<.001f);
        }
        assert(std::memcmp(storage.data()+52,originalStorage.data()+52,20)==0);
    }
    field<float>(m,80)=72; field<float>(m,84)=200;
    field<float>(c,80)=72; field<float>(c,84)=560; field<float>(c,52)=10;
    field<float>(m,48)=field<float>(c,48)=-816;
    for (float y : {-170.f,10.f,190.f}) {
        field<float>(m,52)=y; sampleBand(m,c);
        for (float v : {0.f,.5f,1.f}) {
            const float sv=-field<float>(st,48)*v+.5f-field<float>(st,36)+field<float>(st,48)*.5f;
            assert(std::abs(10+(sv-.5f)*560-(y+(.5f-v)*200))<.001f);
        }
    }
    // Lease actual native centers, preserve their original owning arrays, and
    // remove every borrowed pointer before native destruction can see it.
    std::array<std::array<std::byte,512>,11> nativePanes{}, targets{}, originalPages{};
    std::array<std::uintptr_t,11> originalSlots{}, borrowedSlots{}, originals{}, privateParts{};
    for (unsigned j=0;j<11;++j) {
        auto& center=panel.centers[j];
        originalSlots[j]=100+j;
        center={originalSlots[j], address(nativePanes[j]), 0x8000+j, address(targets[j]),
                reinterpret_cast<std::uintptr_t>(&borrowedSlots[j]), 0x9000+j,
                reinterpret_cast<std::uintptr_t>(&privateParts[j]), 0xa000+j, address(originalPages[j])};
        privateParts[j]=center.savedParts;
        field<std::uintptr_t>(center.pane,24)=center.parent;
        originals[j]=address(originalPages[j]);
    }
    field<std::uintptr_t>(controlPtr,96)=reinterpret_cast<std::uintptr_t>(originals.data());
    field<int>(controlPtr,88)=11;
    leaseCenters(panel,true);
    assert(panel.leased && reparents==11 && borrowedSlots==originalSlots);
    for (unsigned j=0;j<11;++j) assert(privateParts[j]==0x9000+j);
    assert(borrowedOwner(100)==&panel && !borrowedOwner(99));
    assert(SelectBorrowedSlot::Callback(100,true,true)==0);
    assert(BindShortcutItem::Callback(0,100,8,0)==0); // Native refresh cannot replace a leased icon.
    leaseCenters(panel,true); assert(reparents==11);
    leaseCenters(panel,false);
    assert(!panel.leased && reparents==22 && invalidations==1);
    for (unsigned j=0;j<11;++j) {
        assert(borrowedSlots[j]==0 && originalSlots[j]==100+j && privateParts[j]==0xa000+j);
        assert(field<std::uintptr_t>(panel.centers[j].pane,24)==panel.centers[j].parent);
        assert(field<unsigned char>(originals[j],61)==255);
    }
    leaseCenters(panel,false); assert(reparents==22 && invalidations==1);

}
