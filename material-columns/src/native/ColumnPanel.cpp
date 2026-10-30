// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "ColumnPanel.hpp"
#include "MemoryProfile.hpp"
#include "DiagnosticLog.hpp"
#include "Runtime.hpp"
#include "BindingRequest.hpp"
#include "SlotObservation.hpp"
#include "../pure/ScrollMotion.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <array>
#include <cstring>
#include <utility>
#include <lib.hpp>

namespace material_columns::native {
namespace {
std::uintptr_t base = 0;
bool installed = false;
struct Panel {
    std::uintptr_t owner{}, child{}, controller{}, layout{}, pane{}, stockPane{};
    BindColumnItem bind{};
    void* context{};
    DescribeColumn describe{};
    std::array<float, maximumColumns> vertical{};
    std::array<std::array<std::uintptr_t, 2>, 3> masks{};
    struct Borrowed {
        std::uintptr_t slot{}, pane{}, parent{}, target{}, destination{};
        std::uintptr_t parts{}, partsDestination{}, savedParts{}, page{};
    };
    std::array<Borrowed, 11> centers{};
    struct Outer {
        std::uintptr_t pane{}, slot{}, parts{}, ownerSlot{}, page{};
        int group{-1};
        SlotPresentation presentation{};
        bool contentBound{};
        std::uint64_t traceState{};
    };
    std::array<Outer, 6> outer{};
    std::uintptr_t outerPark{};
    unsigned pagesCreated{}, poolUpdates{}, poolRetained{};
    bool outerReady{}, outerBinding{};
    std::array<std::uintptr_t, 2> bands{};
    bool leased{}, binding{};
    std::size_t count{};
    unsigned calculations{}, updates{}, rowTemplateCopies{};
    float guideY{}, sortY{};
    bool layoutValid{}, ready{}, visible{}, stockWasVisible{};
};
// Screen destruction clears these before game-owned children/controllers are destroyed.
std::array<Panel, 4> panels{};
template<class T> T& field(std::uintptr_t object, std::size_t offset) {
    return *reinterpret_cast<T*>(object + offset);
}
#ifdef MATERIAL_COLUMNS_PANEL_HOST_TEST
std::uintptr_t resolvePanelTestFunction(std::uintptr_t offset);
template<class Function> Function function(std::uintptr_t offset) {
    return reinterpret_cast<Function>(resolvePanelTestFunction(offset));
}
#else
template<class Function> Function function(std::uintptr_t offset) {
    return reinterpret_cast<Function>(base + offset);
}
#endif
Panel* find(std::uintptr_t screen) {
    for (auto& panel : panels) if (panel.owner == screen && screen) return &panel;
    return nullptr;
}
Panel* forController(std::uintptr_t controller) {
    for (auto& panel : panels) if (panel.controller == controller && controller) return &panel;
    return nullptr;
}
void visible(std::uintptr_t pane, bool value) {
    // Pane::Initialize copies BFLYT flags+8 to Pane+88; bit0 is native visibility.
    auto& flags = field<std::uint8_t>(pane, 88);
    flags = static_cast<std::uint8_t>((flags & ~1u) | (value ? 1u : 0u) | 0x10u);
}
void reparent(std::uintptr_t pane, std::uintptr_t parent) {
    function<void (*)(std::uintptr_t, std::uintptr_t)>(0xd84d30)(field<std::uintptr_t>(pane, 24), pane);
    function<void (*)(std::uintptr_t, std::uintptr_t)>(0x700480)(parent, pane);
}
void clearSlot(std::uintptr_t slot) {
    function<void (*)(std::uintptr_t, int)>(0x11f6dcc)(slot, 0);
}
void synchronizeAnimations(Panel& panel) {
    const auto stock = field<std::uintptr_t>(panel.owner, 424);
    if (!stock) return;
    const auto stockLayout = field<std::uintptr_t>(stock, 32);
    field<std::uint8_t>(panel.layout, 145) = field<std::uint8_t>(stockLayout, 145);
    for (std::size_t offset = 48; offset <= 72; offset += 8) {
        const auto source = field<std::uintptr_t>(stock, offset);
        const auto target = field<std::uintptr_t>(panel.child, offset);
        if (!source || !target) continue;
        // StopCurrent's frame/playing fields, followed by the same native Animate call.
        // Do not emit its screen notification; the stock animator owns lifecycle events.
        field<float>(target, 32) = field<float>(source, 32);
        field<std::uint32_t>(target, 80) = 0;
        field<std::uint16_t>(target, 84) &= 0xfff0;
        reinterpret_cast<void (*)(std::uintptr_t, bool)>(field<std::uintptr_t>(field<std::uintptr_t>(target, 0), 32))(target, true);
    }
}
void positionPresentation(Panel& panel) {
    using Lookup = std::uintptr_t (*)(std::uintptr_t, const char*);
    // Stock animation tracks may set the caption translations; retain the private spacing.
    for (const auto& position : {std::pair{"N_AttachmentLis_02", 350.f}, std::pair{"A_ScrollBarText_00", -320.f}}) {
        const auto pane = function<Lookup>(0x7012ec)(panel.layout, position.first);
        if (pane) { field<float>(pane, 52) = position.second; field<std::uint8_t>(pane, 88) |= 0x10; }
    }
    const auto parentLayout = field<std::uintptr_t>(panel.owner, 40);
    for (const auto& position : {std::pair{"N_Guide_00", -470.f}, std::pair{"Pa_SortWindow_00", -230.f}}) {
        const auto pane = function<Lookup>(0x7012ec)(parentLayout, position.first);
        if (pane) { field<float>(pane, 52) = position.second; field<std::uint8_t>(pane, 88) |= 0x10; }
    }
}
void sampleBand(std::uintptr_t mask, std::uintptr_t capture) {
    if (!mask || !capture) return;
    const auto material = field<std::uintptr_t>(mask, 216);
    if (!material) return;
    // Material::SetupSubmaterialOf_TextureMatrix: capacity counts at +16,
    // storage +24, TextureMap[capacity] followed by TexSRT[capacity].
    const auto capacity = field<unsigned>(material, 16);
    if ((capacity & 3u) != 2 || ((capacity >> 2) & 3u) != 2) return;
    const auto srt = field<std::uintptr_t>(material, 24) + 16 * (capacity & 3u);
    const float width = field<float>(capture, 80), height = field<float>(capture, 84);
    if (width <= 0 || height <= 0) return;
    field<float>(srt, 0) = (field<float>(mask, 48) - field<float>(capture, 48)) / width;
    field<float>(srt, 4) = -(field<float>(mask, 52) - field<float>(capture, 52)) / height;
    field<float>(srt, 8) = 0;
    field<float>(srt, 12) = field<float>(mask, 80) / width;
    field<float>(srt, 16) = field<float>(mask, 84) / height;
    // The second map/SRT remains the accepted full-resolution outward gradient.
}
void horizontalMasks(Panel& panel, bool show) {
    const auto parent = field<std::uintptr_t>(panel.owner, 40);
    for (const char side : {'L', 'R'}) {
        char captureName[] = "C_CaptureL_00"; captureName[9] = side;
        const auto capture = function<std::uintptr_t (*)(std::uintptr_t, const char*)>(0x7012ec)(parent, captureName);
        if (capture) {
            field<float>(capture, 84) = 200.f;
            field<std::uint8_t>(capture, 88) |= 0x10;
            memory::registerCapture(panel.owner, capture);
            memory::captureEnabled(capture, show);
        }
        char maskName[] = "P_MaskL_00"; maskName[6] = side;
        const auto mask = function<std::uintptr_t (*)(std::uintptr_t, const char*)>(0x7012ec)(parent, maskName);
        if (show || panel.layoutValid) sampleBand(mask, capture);
    }
}
void hideVerticalMasks(const Panel& panel) {
    for (const auto capture : panel.bands) if (capture) visible(capture, false);
    for (const auto& page : panel.masks) for (const auto mask : page)
        if (mask) visible(mask, false);
}
void initializeVerticalMasks(Panel& panel) {
    const auto parent = field<std::uintptr_t>(panel.owner, 40);
    using Lookup = std::uintptr_t (*)(std::uintptr_t, const char*);
    for (std::size_t side = 0; side < 2; ++side) {
        char capture[] = "C_CaptureU_00"; capture[9] = side ? 'D' : 'U';
        panel.bands[side] = function<Lookup>(0x7012ec)(parent, capture);
        memory::registerCapture(panel.owner, panel.bands[side]);
    }
    for (std::size_t i = 0; i < panel.masks.size(); ++i) for (std::size_t side = 0; side < 2; ++side) {
        char mask[] = "P_MaskU_00";
        mask[6] = side ? 'D' : 'U';
        mask[8] = static_cast<char>('0' + i / 10);
        mask[9] = static_cast<char>('0' + i % 10);
        panel.masks[i][side] = function<Lookup>(0x7012ec)(parent, mask);
    }
    hideVerticalMasks(panel);
}
bool independentMaskMaterials(const Panel& panel) {
    std::array<std::uintptr_t, 8> materials{};
    unsigned count = 0;
    const auto accept = [&](std::uintptr_t pane) {
        if (!pane) return false;
        const auto material = field<std::uintptr_t>(pane, 216);
        if (!material || (field<unsigned>(material, 16) & 15u) != 10u) return false;
        for (unsigned i = 0; i < count; ++i) if (materials[i] == material) return false;
        materials[count++] = material;
        return true;
    };
    for (const auto& page : panel.masks) for (const auto mask : page) if (!accept(mask)) return false;
    const auto parent = field<std::uintptr_t>(panel.owner, 40);
    for (const char side : {'L', 'R'}) {
        char name[] = "P_MaskL_00"; name[6] = side;
        if (!accept(function<std::uintptr_t (*)(std::uintptr_t, const char*)>(0x7012ec)(parent, name))) return false;
    }
    Logging.Log("[material-columns] memory fade-bands=4 independent-mask-materials=%u", count);
    return true;
}
Panel* borrowedOwner(std::uintptr_t slot) {
    if (slot) for (auto& panel : panels) if (panel.leased)
        for (const auto& center : panel.centers) if (center.slot == slot) return &panel;
    return nullptr;
}
bool prepareCenters(Panel& panel) {
    const auto stock = field<std::uintptr_t>(panel.owner, 424);
    const auto control = stock ? field<std::uintptr_t>(stock, 80) : 0;
    if (!control || field<int>(control, 88) != 11 || field<int>(panel.controller, 88) != 11) return false;
    for (unsigned i = 0; i < 11; ++i) {
        const auto original = field<std::uintptr_t>(field<std::uintptr_t>(control, 96), i * 8);
        const auto page = field<std::uintptr_t>(field<std::uintptr_t>(panel.controller, 96), i * 8);
        if (!original || !page || field<unsigned>(original, 64) != 1 || field<unsigned>(page, 64) != 3) return false;
        const auto pane = field<std::uintptr_t>(field<std::uintptr_t>(original, 24), 0);
        const auto slot = field<std::uintptr_t>(field<std::uintptr_t>(original, 72), 0);
        const auto placeholder = field<std::uintptr_t>(field<std::uintptr_t>(page, 24), 0);
        const auto layout = placeholder ? field<std::uintptr_t>(placeholder, 232) : 0;
        const auto target = layout ? field<std::uintptr_t>(layout, 24) : 0;
        if (!pane || !slot || !target || !field<std::uintptr_t>(pane, 24)) return false;
        if (field<unsigned>(original, 32) != 1 || field<unsigned>(page, 32) != 3) return false;
        const auto parts = field<std::uintptr_t>(field<std::uintptr_t>(original, 40), 0);
        const auto destination = field<std::uintptr_t>(page, 40);
        if (!parts || !destination) return false;
        panel.centers[i] = {slot, pane, field<std::uintptr_t>(pane, 24), target, field<std::uintptr_t>(page, 72),
                           parts, destination, field<std::uintptr_t>(destination, 0), page};
    }
    Logging.Log("[material-columns] memory native-slots reusable=11 private-slots=6 ownership=borrowed outer-pool=6");
    return true;
}
void leaseCenters(Panel& panel, bool borrow) {
    if (panel.leased == borrow) return;
    for (const auto& center : panel.centers) {
        reparent(center.pane, borrow ? center.target : center.parent);
        field<std::uintptr_t>(center.destination, 0) = borrow ? center.slot : 0;
        // The native generic PartsScreen drives animation and visibility. Borrow
        // that controller reference too, not just its pixels and slot wrapper.
        field<std::uintptr_t>(center.partsDestination, 0) = borrow ? center.parts : center.savedParts;
        field<std::uint8_t>(center.page, 60) = 0xff;
        field<std::uint8_t>(center.page, 61) = 0xff;
    }
    panel.leased = borrow;
    if (!borrow) {
        const auto stock = field<std::uintptr_t>(panel.owner, 424);
        const auto control = stock ? field<std::uintptr_t>(stock, 80) : 0;
        if (control) for (unsigned i = 0; i < 11; ++i) {
            const auto page = field<std::uintptr_t>(field<std::uintptr_t>(control, 96), i * 8);
            field<std::uint8_t>(page, 60) = 0xff;
            field<std::uint8_t>(page, 61) = 0xff;
        }
        if (control) { field<std::uint8_t>(control, 190) |= 0x62; function<void (*)(std::uintptr_t)>(0x1a36180)(control); }
    }
}
HOOK_DEFINE_TRAMPOLINE(CopyColumnRowLayout) {
    static std::uintptr_t Callback(std::uintptr_t controller, std::uintptr_t heap,
                                   std::uintptr_t source, std::uintptr_t destination) {
        if (auto* panel = forController(controller); panel && source && destination) {
            // Native setUpElementPanes_ passes row zero as the source for every cloned row. Our row zero is inert; each outer row must copy its own native controllers (including nested icon controllers) instead.
            const auto pages = field<std::uintptr_t>(controller, 96);
            const auto first = pages ? field<std::uintptr_t>(pages, 0) : 0;
            const auto rows = first ? field<std::uintptr_t>(first, 24) : 0;
            bool matched = false;
            if (rows) for (unsigned row = 0; row < field<unsigned>(first, 16); ++row) {
                const auto candidate = field<std::uintptr_t>(rows, row * 8);
                if (candidate && std::strcmp(reinterpret_cast<const char*>(candidate + 176),
                                             reinterpret_cast<const char*>(destination + 176)) == 0) {
                    if (candidate != source) ++panel->rowTemplateCopies;
                    source = candidate;
                    matched = true;
                    break;
                }
            }
            if (!matched) Logging.Log("[material-columns] row template missing: pane=%s first=%llx",
                reinterpret_cast<const char*>(destination + 176), static_cast<unsigned long long>(first));
        }
        return Orig(controller, heap, source, destination);
    }
};
bool createOuterTiles(Panel& panel, std::uintptr_t heap) {
    memory::HeapSample poolSample(heap, "six-native-outer-clones", true);
    const auto stock = field<std::uintptr_t>(panel.owner, 424);
    const auto control = stock ? field<std::uintptr_t>(stock, 80) : 0;
    const auto pages = control ? field<std::uintptr_t>(control, 96) : 0;
    const auto page = pages ? field<std::uintptr_t>(pages, 0) : 0;
    const auto panes = page ? field<std::uintptr_t>(page, 24) : 0;
    const auto source = panes ? field<std::uintptr_t>(panes, 0) : 0;
    if (!source) { Logging.Log("[material-columns] outer pool refused: native template absent"); return false; }
    panel.outerPark = field<std::uintptr_t>(panel.layout, 24);
    for (unsigned i = 0; i < panel.outer.size(); ++i) {
        const auto pane = function<std::uintptr_t (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, bool)>(0x6ff4e0)(0, source, panel.layout, false);
        if (!pane) { Logging.Log("[material-columns] outer pool allocation refused: index=%u", i); return false; }
        char name[] = "Pa_MCOuter_00"; name[12] = static_cast<char>('0' + i);
        function<void (*)(std::uintptr_t, const char*)>(0x6ff27c)(pane, name);
        function<void (*)(std::uintptr_t, std::uintptr_t)>(0x700480)(panel.outerPark, pane);
        function<std::uintptr_t (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t)>(0xf84cf0)(
            panel.owner, heap, field<std::uintptr_t>(source, 232), field<std::uintptr_t>(pane, 232));
        for (unsigned offset : {48u, 52u, 56u}) field<float>(pane, offset) = 0;
        visible(pane, false);
        panel.outer[i].pane = pane;
    }
    return true;
}
HOOK_DEFINE_TRAMPOLINE(InitializeColumnPage) {
    static void Callback(std::uintptr_t page, const std::uintptr_t* args) {
        auto* panel = args ? forController(args[1]) : nullptr;
        if (!panel) { Orig(page, args); return; }
        const auto index = panel->pagesCreated++;
        std::array<std::uintptr_t, 3> panes{}, layouts{};
        for (unsigned row = 0; row < 3; ++row) {
            panes[row] = field<std::uintptr_t>(args[3], row * 8);
            layouts[row] = field<std::uintptr_t>(panes[row], 232);
            // Only the first three pages own outer slot wrappers. Their real panes
            // live in the six-tile pool; every visible row remains an inert container.
            if (row && index < 3)
                field<std::uintptr_t>(args[3], row * 8) = panel->outer[index * 2 + row - 1].pane;
            else field<std::uintptr_t>(panes[row], 232) = 0;
        }
        Orig(page, args);
        const auto slots = field<std::uintptr_t>(page, 72);
        for (unsigned row = 0; row < 3; ++row) {
            field<std::uintptr_t>(args[3], row * 8) = panes[row];
            field<std::uintptr_t>(panes[row], 232) = layouts[row];
            if (!slots) continue;
            if (row && index < 3) {
                auto& tile = panel->outer[index * 2 + row - 1];
                tile.ownerSlot = slots + row * 8;
                tile.slot = field<std::uintptr_t>(tile.ownerSlot, 0);
            } else field<std::uintptr_t>(slots, row * 8) = 0;
        }
    }
};
Panel* outerOwner(std::uintptr_t slot) {
    if (slot) for (auto& panel : panels) if (panel.outerReady)
        for (const auto& tile : panel.outer) if (tile.slot == slot) return &panel;
    return nullptr;
}
bool prepareOuterTiles(Panel& panel) {
    for (auto& tile : panel.outer) {
        tile.parts = tile.slot ? field<std::uintptr_t>(tile.slot, 168) : 0;
        if (!tile.parts || field<std::uintptr_t>(tile.parts, 32) != field<std::uintptr_t>(tile.pane, 232)) {
            Logging.Log("[material-columns] outer pool controller refused: slot=%llx part=%llx",
                static_cast<unsigned long long>(tile.slot), static_cast<unsigned long long>(tile.parts));
            return false;
        }
    }
    for (auto& tile : panel.outer) {
        // Outer tiles are presentation-only. Native page activation must operate
        // on the inert row controllers, never on these reusable icon controllers.
        field<unsigned>(tile.parts, 64) = (field<unsigned>(tile.parts, 64) | 0x10000) & ~0x10u;
        field<int>(tile.parts, 68) = -1;
        field<std::uintptr_t>(tile.ownerSlot, 0) = 0;
    }
    panel.outerReady = true;
    Logging.Log("[material-columns] memory outer pool tiles=6 native-centers=11 opacity-extra-textures=0");
    return true;
}
void parkOuter(Panel& panel, Panel::Outer& tile) {
    if (!tile.page) return;
    reparent(tile.pane, panel.outerPark);
    visible(tile.pane, false);
    tile.page = 0; tile.group = -1; tile.contentBound = false;
}
void parkOuterTiles(Panel& panel, bool restoreOwnership = false) {
    if (!panel.outerReady) return;
    for (unsigned i = 0; i < panel.outer.size(); ++i) {
        auto& tile = panel.outer[i];
        parkOuter(panel, tile);
        if (restoreOwnership) field<std::uintptr_t>(tile.ownerSlot, 0) = tile.slot;
    }
}
void placeOuterTiles(Panel& panel) {
    if (!panel.outerReady || !panel.visible) return;
    const auto pages = field<std::uintptr_t>(panel.controller, 96);
    const auto count = field<unsigned>(panel.controller, 88);
    int centered = -1; float nearest = 1e9f;
    // Switch the three-page window halfway through a native horizontal step.
    // At that point the outgoing/incoming outer tiles both have zero opacity.
    for (unsigned i = 0; i < count; ++i) {
        const auto page = field<std::uintptr_t>(pages, i * 8);
        const int group = field<int>(page, 56);
        if (group < 0 || static_cast<std::size_t>(group) >= panel.count) continue;
        const float distance = std::abs(field<float>(field<std::uintptr_t>(page, 48), 48));
        if (distance < nearest) { nearest = distance; centered = group; }
    }
    std::array<std::uintptr_t, 3> targets{};
    if (centered >= 0) for (unsigned i = 0; i < count; ++i) {
        const auto page = field<std::uintptr_t>(pages, i * 8);
        const int group = field<int>(page, 56);
        if (group >= 0 && static_cast<std::size_t>(group) < panel.count && std::abs(group - centered) <= 1)
            targets[static_cast<unsigned>(group) % 3] = page;
    }
    for (unsigned i = 0; i < panel.outer.size(); ++i) {
        auto& tile = panel.outer[i];
        if (tile.page != targets[i / 2]) parkOuter(panel, tile);
    }
    for (unsigned i = 0; i < panel.outer.size(); ++i) {
        auto& tile = panel.outer[i]; const unsigned row = i % 2 + 1;
        const auto page = targets[i / 2];
        if (!page) continue;
        if (!tile.page) {
            const auto container = field<std::uintptr_t>(field<std::uintptr_t>(page, 24), row * 8);
            const auto target = field<std::uintptr_t>(field<std::uintptr_t>(container, 232), 24);
            reparent(tile.pane, target);
            // Leave page+40 on the inert button and page+72 empty. onInactive_ clears icons/quantities and enables button animations through +72; page updates select/reindex buttons through +40 independently of us.
            tile.page = page;
        }
        const int group = field<int>(page, 56);
        if (tile.group != group) tile.contentBound = false;
        tile.group = group;
    }
}
bool hasOuterTiles(const Panel& panel, std::uintptr_t page) {
    if (!panel.outerReady) return false;
    for (const auto& tile : panel.outer) if (tile.page == page) return true;
    return false;
}
void bindOuterTiles(Panel& panel);
HOOK_DEFINE_TRAMPOLINE(SelectBorrowedSlot) {
    static std::uintptr_t Callback(std::uintptr_t slot, bool selected, bool animate) {
        const auto* panel = borrowedOwner(slot);
        if (!panel) panel = outerOwner(slot);
        if (panel && !panel->binding) return 0;
        return Orig(slot, selected, animate);
    }
};
std::uintptr_t captionPane(std::uintptr_t page) {
    const auto root = field<std::uintptr_t>(page, 48);
    return root ? function<std::uintptr_t (*)(std::uintptr_t, const char*, bool)>(0x470f8)(root, "T_MCGroup_00", true) : 0;
}
void setCaption(Panel& panel, std::uintptr_t page, std::size_t group) {
    const auto pane = captionPane(page);
    if (!pane) return;
    const auto appearance = panel.describe && group < panel.count ? panel.describe(group) : ColumnAppearance{};
    const auto root = field<std::uintptr_t>(page, 48);
    const float opacity = hasOuterTiles(panel, page) && root ? columnLabelOpacity(field<float>(root, 48)) : 0.f;
    field<std::uint8_t>(pane, 89) = static_cast<std::uint8_t>(255.f * opacity);
    visible(pane, appearance.label && group < panel.count && opacity > 0.f);
    if (!appearance.label || group >= panel.count) return;
    char16_t text[64]{};
    unsigned length = 0;
    // Catalog family captions are controlled ASCII labels, not localized item names.
    while (length + 1 < std::size(text) && appearance.label[length]) {
        text[length] = static_cast<unsigned char>(appearance.label[length]);
        ++length;
    }
    // SetString invalidates draw/glyph state even for unchanged text. Recycled pages
    // need a new caption only when the actual string changes, not every calculation.
    const auto buffer = field<std::uintptr_t>(pane, 216);
    if (buffer && field<std::uint16_t>(pane, 274) == length &&
        std::memcmp(reinterpret_cast<const void*>(buffer), text, length * sizeof(char16_t)) == 0) return;
    function<void (*)(std::uintptr_t, const char16_t*, unsigned)>(0x22acb04)(pane, text, length);
    // SetString does not invalidate ScalableFontTextBoxEx's glyph readiness. Keep a
    // pending queue link intact; otherwise request registration for the new label.
    if (field<std::uint8_t>(pane, 360) != 1) field<std::uint8_t>(pane, 360) = 0;
}
void updateRows(Panel& panel, float frames, bool finalPresentation = false) {
    const auto stock = field<std::uintptr_t>(panel.owner, 424);
    const auto control = stock ? field<std::uintptr_t>(stock, 80) : 0;
    if (!control || !panel.describe) return;
    const auto fraction = field<float>(control, 72), minimum = field<float>(control, 76);
    for (auto& offset : panel.vertical) offset = scrollToward(offset, 0, frames, fraction, minimum);
    const auto pages = field<std::uintptr_t>(panel.controller, 96);
    const auto count = field<int>(panel.controller, 88);
    placeOuterTiles(panel);
    if (finalPresentation && panel.outerReady) bindOuterTiles(panel);
    hideVerticalMasks(panel);
    for (int i = 0; i < count; ++i) {
        const auto page = field<std::uintptr_t>(pages, static_cast<std::size_t>(i) * 8);
        const auto group = field<int>(page, 56);
        if (group < 0 || static_cast<std::size_t>(group) >= panel.count) {
            if (const auto caption = captionPane(page)) visible(caption, false);
            continue;
        }
        if (finalPresentation) setCaption(panel, page, static_cast<std::size_t>(group));
        const auto appearance = panel.describe(static_cast<std::size_t>(group));
        const auto pagePane = field<std::uintptr_t>(page, 48);
        const bool outerShown = hasOuterTiles(panel, page);
        if (pagePane && outerShown) {
            const float x = field<float>(pagePane, 48) + field<float>(panel.pane, 48);
            const float y = field<float>(pagePane, 52) + field<float>(panel.pane, 52);
            for (std::size_t side = 0; side < 2; ++side) {
                const bool show = (side ? appearance.below : appearance.above) &&
                                  (field<std::uint8_t>(pagePane, 88) & 1) && std::abs(x) < 935.f;
                const auto capture = panel.bands[side], mask = panel.masks[static_cast<unsigned>(group) % 3][side];
                if (capture) {
                    field<float>(capture, 52) = y + (side ? -234.f : 234.f);
                    field<std::uint8_t>(capture, 88) |= 0x10;
                    if (show) visible(capture, true);
                }
                if (mask) {
                    field<float>(mask, 48) = x;
                    field<float>(mask, 52) = y + (side ? -234.f : 234.f);
                    field<std::uint8_t>(mask, 89) = field<std::uint8_t>(pagePane, 89);
                    visible(mask, show);
                    sampleBand(mask, capture);
                }
            }
        }
        const auto parts = field<std::uintptr_t>(page, 24);
        if (!parts || field<int>(page, 16) != 3) continue;
        for (std::size_t row = 0; row < 3; ++row) {
            const auto pane = field<std::uintptr_t>(parts, row * 8);
            if (!pane) continue;
            const float y = (row == 0 ? 0.f : row == 1 ? rowPitch : -rowPitch) + panel.vertical[group];
            field<float>(pane, 52) = y;
            field<std::uint8_t>(pane, 88) |= 0x10;
            field<std::uint8_t>(pane, 89) = static_cast<std::uint8_t>(255.f * rowOpacity(y) * (row ? outerColumnOpacity(field<float>(pagePane, 48)) : 1.f));
            if (finalPresentation && row == 0 && panel.leased) {
                const auto& center = panel.centers[static_cast<std::size_t>(i)];
                visible(center.pane, true);
                field<std::uint8_t>(center.pane, 89) = 255;

            }
            if (finalPresentation && panel.bind) {
                std::uint32_t nativeIndex{}; bool selected{};
                const bool populated = (!row || outerShown) && panel.bind(panel.context, static_cast<std::uint32_t>(group * 3 + row), nativeIndex, selected);
                visible(pane, populated);
                const auto slots = field<std::uintptr_t>(page, 72);
                const auto slot = slots && row < field<unsigned>(page, 64) ? field<std::uintptr_t>(slots, row * 8) : 0;
                if (slot) function<void (*)(std::uintptr_t, int)>(0x11f6dcc)(slot, populated);
            }
        }
    }
}

void create(std::uintptr_t screen, std::uintptr_t heap) {
    if (find(screen)) return;
    Panel* panel = nullptr;
    for (auto& candidate : panels) if (!candidate.owner) { panel = &candidate; break; }
    if (!panel) {
        Logging.Log("[material-columns] private panel refused: screen registry full owner=%llx",
                    static_cast<unsigned long long>(screen));
        return;
    }
    const auto parentLayout = field<std::uintptr_t>(screen, 40);
    using Lookup = std::uintptr_t (*)(std::uintptr_t, const char*);
    const auto layout = function<Lookup>(0x70156c)(parentLayout, "Pa_MaterialColumns_00");
    const auto pane = function<Lookup>(0x7012ec)(parentLayout, "Pa_MaterialColumns_00");
    const auto stockPane = function<Lookup>(0x7012ec)(parentLayout, "Pa_SlideListB_00");
    using LookupControl = std::uintptr_t (*)(std::uintptr_t, const char*, std::uintptr_t);
    const auto controller = layout ? function<LookupControl>(0x10562d0)(screen, "N_SlideList_00", layout) : 0;
    if (!layout || !pane || !stockPane || !controller) {
        Logging.Log("[material-columns] private panel missing layout=%llx pane=%llx stock=%llx control=%llx",
                    static_cast<unsigned long long>(layout), static_cast<unsigned long long>(pane),
                    static_cast<unsigned long long>(stockPane), static_cast<unsigned long long>(controller));
        return;
    }
    memory::HeapSample heapSample(heap, "private-controller-and-pages");
    using Allocate = std::uintptr_t (*)(std::size_t, std::uintptr_t, std::uint32_t);
    const auto child = function<Allocate>(0x7fa8f0)(192, heap, 8);
    if (!child) {
        Logging.Log("[material-columns] private panel allocation failed: bytes=192 heap=%llx",
                    static_cast<unsigned long long>(heap));
        return;
    }
    function<void (*)(std::uintptr_t, std::uintptr_t)>(0x123a39c)(child, layout);
    *panel = {screen, child, controller, layout, pane, stockPane};
    horizontalMasks(*panel, false); // Native capture setup initially forces capture visibility.
    initializeVerticalMasks(*panel);
    field<std::uintptr_t>(child, 80) = controller;
    if (!createOuterTiles(*panel, heap)) return;
    struct InitArg { std::uintptr_t heap; std::int32_t count; std::uint32_t padding; };
    static_assert(sizeof(InitArg) == 16);
    InitArg args{heap, static_cast<std::int32_t>(maximumColumns * 3), 0};
    function<void (*)(std::uintptr_t, const InitArg*)>(0x123a414)(controller, &args);
    field<std::int32_t>(child, 148) = -1;
    const auto columns = field<std::int32_t>(controller, 48);
    const auto rows = field<std::int32_t>(controller, 52);
    if (columns != 1 || rows != 3 || !field<std::uintptr_t>(controller, 96) ||
        field<int>(controller, 88) > static_cast<int>(panel->centers.size())) {
        Logging.Log("[material-columns] private grid refused: columns=%d rows=%d pages=%llx",
                    columns, rows, static_cast<unsigned long long>(field<std::uintptr_t>(controller, 96)));
        return;
    }
    function<void (*)(std::uintptr_t, int)>(0x1a8bf4c)(controller, 0);
    visible(pane, false);
    for (const auto& page : panel->masks) for (unsigned side = 0; side < 2; ++side) {
        if (!panel->bands[side] || !page[side]) {
            Logging.Log("[material-columns] vertical overflow pane missing capture=%llx mask=%llx",
                static_cast<unsigned long long>(panel->bands[side]), static_cast<unsigned long long>(page[side]));
            return;
        }
    }
    if (!independentMaskMaterials(*panel)) { Logging.Log("[material-columns] shared fade material validation refused"); return; }
    panel->layoutValid = true;
}

HOOK_DEFINE_TRAMPOLINE(CreateShortcutParts) {
    static std::uintptr_t Callback(std::uintptr_t screen, std::uintptr_t heap) {
        const auto result = Orig(screen, heap);
        create(screen, heap);
        return result;
    }
};
bool validateColumnRows(const Panel& panel) {
    const auto count = field<unsigned>(panel.controller, 88);
    const auto pages = field<std::uintptr_t>(panel.controller, 96);
    if (!pages || !count || count > panel.centers.size()) {
        Logging.Log("[material-columns] row contract refused: pages=%llx count=%u",
            static_cast<unsigned long long>(pages), count);
        return false;
    }
    for (unsigned i = 0; i < count; ++i) {
        const auto page = field<std::uintptr_t>(pages, i * 8);
        const auto parts = page ? field<std::uintptr_t>(page, 40) : 0;
        const auto slots = page ? field<std::uintptr_t>(page, 72) : 0;
        if (!parts || !slots || field<unsigned>(page, 32) != 3 || field<unsigned>(page, 64) != 3) {
            Logging.Log("[material-columns] row contract refused: page=%u parts=%llx slots=%llx", i,
                static_cast<unsigned long long>(parts), static_cast<unsigned long long>(slots));
            return false;
        }
        for (unsigned row = 0; row < 3; ++row) {
            const auto part = field<std::uintptr_t>(parts, row * 8);
            const auto slot = field<std::uintptr_t>(slots, row * 8);
            const auto expected = row && i < 3 ? panel.outer[i * 2 + row - 1].slot : 0;
            if (!part || slot != expected || (row && i < 3 && (!slot ||
                field<std::uintptr_t>(slot, 32) != field<std::uintptr_t>(panel.outer[i * 2 + row - 1].pane, 232)))) {
                Logging.Log("[material-columns] row contract refused: page=%u row=%u part=%llx slot=%llx", i, row,
                    static_cast<unsigned long long>(part), static_cast<unsigned long long>(slot));
                return false;
            }
        }
    }
    Logging.Log("[material-columns] row contract ready: pages=%u private-slots=6 pooled=1", count);
    return true;
}
HOOK_DEFINE_TRAMPOLINE(InitializeShortcutParts) {
    static std::uintptr_t Callback(std::uintptr_t screen, std::uintptr_t heap) {
        Logging.Log("[material-columns] initialize stock begin");
        const auto result = Orig(screen, heap);
        Logging.Log("[material-columns] initialize stock complete");
        auto* panel = find(screen);
        if (!panel || !panel->child || !panel->layoutValid) return result;
        using Animator = std::uintptr_t (*)(std::uintptr_t, const char*);
        for (const char* name : {"Fade", "IsMainItemDemo", "ListPattern", "Invalid"}) {
            if (!function<Animator>(0x10a20c8)(panel->layout, name)) {
                Logging.Log("[material-columns] private panel animator missing: %s", name);
                return result;
            }
        }
        if (!validateColumnRows(*panel)) return result;
        Logging.Log("[material-columns] initialize private begin row-template-copies=%u", panel->rowTemplateCopies);
        memory::HeapSample heapSample(heap, "private-child-initialize");
        function<void (*)(std::uintptr_t, std::uintptr_t)>(0x1228ae4)(panel->child, heap);
        if (!prepareOuterTiles(*panel)) return result;
        if (!prepareCenters(*panel)) { Logging.Log("[material-columns] native center-slot reuse refused"); return result; }
        panel->ready = true;
        Logging.Log("[material-columns] initialize private complete");
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(UpdateShortcutParts) {
    static std::uintptr_t Callback(std::uintptr_t child) {
        for (const auto& panel : panels) if (panel.child == child && child) return 0;
        const auto result = updateMaterialShortcut(child, [](std::uintptr_t value) { return Orig(value); });
        pumpDiagnostics();
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(UpdateColumnController) {
    static std::uintptr_t Callback(std::uintptr_t controller, float frames) {
        for (const auto& candidate : panels) if (candidate.leased) {
            const auto stock = field<std::uintptr_t>(candidate.owner, 424);
            if (stock && field<std::uintptr_t>(stock, 80) == controller) return 0;
        }
        auto* panel = forController(controller);
        if (panel && panel->ready && panel->visible) {
            synchronizeAnimations(*panel);
            field<std::uint8_t>(controller, 195) = 255;
            field<std::uintptr_t>(controller, 208) = 0;
            field<std::uintptr_t>(controller, 216) = 0;
        }
        const auto result = Orig(controller, frames);
        if (panel && panel->ready && panel->visible) {
            updateRows(*panel, frames);
            if (++panel->updates == 90 && !panel->calculations) {
                const auto layout = field<std::uintptr_t>(panel->owner, 40);
                Logging.Log("[material-columns] presentation calculator missed: layout=%llx dispatch=+%llx",
                    static_cast<unsigned long long>(layout),
                    static_cast<unsigned long long>(field<std::uintptr_t>(field<std::uintptr_t>(layout, 0), 184) - base));
            }
        }
        return result;
    }
};
// Screen::update dispatches LayoutEx virtual184; the base Layout calculator is bypassed. LayoutEx::CalculateImpl computes pane matrices. Apply private presentation here so later native tracks cannot win.
[[maybe_unused]] void traceOuterTiles(Panel& panel) {
    static unsigned remaining=144;
    for (unsigned i=0;remaining && i<panel.outer.size();++i) {
        auto& tile=panel.outer[i];
        if (!tile.page || !tile.slot) continue;
        const auto layout=field<std::uintptr_t>(tile.slot,32);
        const auto root=layout?field<std::uintptr_t>(layout,24):0;
        const auto icon=field<std::uintptr_t>(tile.slot,184);
        const auto iconLayout=icon?field<std::uintptr_t>(icon,32):0;
        const auto iconRoot=iconLayout?field<std::uintptr_t>(iconLayout,24):0;
        const unsigned flags=icon?field<std::uint8_t>(icon,360):0;
        const unsigned buttonFlags=tile.parts?field<unsigned>(tile.parts,64):0;
        std::uint64_t item{};std::memcpy(&item,tile.presentation.bytes.data(),sizeof(item));
        const auto signature=item ^ (std::uint64_t(tile.group+1)<<48) ^ (std::uint64_t(flags)<<32) ^
            buttonFlags ^ (std::uint64_t(root && (field<std::uint8_t>(root,88)&1))<<40) ^
            (std::uint64_t(iconRoot && (field<std::uint8_t>(iconRoot,88)&1))<<41);
        if (signature==tile.traceState) continue;
        tile.traceState=signature;--remaining;
        Logging.Log("[material-columns] pool draw frame=%u tile=%u group=%d row=%u item=%llx root-visible=%u alpha=%u icon-visible=%u icon-alpha=%u icon-flags=%u button-flags=%x button-index=%d page-slot=%llx updates=%u retained=%u",
            panel.calculations,i,tile.group,i%2+1,static_cast<unsigned long long>(item),
            root?field<std::uint8_t>(root,88)&1:0,root?field<std::uint8_t>(root,90):0,
            iconRoot?field<std::uint8_t>(iconRoot,88)&1:0,
            iconRoot?field<std::uint8_t>(iconRoot,90):0,flags,buttonFlags,
            tile.parts?field<int>(tile.parts,68):-1,
            static_cast<unsigned long long>(field<std::uintptr_t>(field<std::uintptr_t>(tile.page,72),(i%2+1)*8)),
            panel.poolUpdates,panel.poolRetained);
    }
}
HOOK_DEFINE_TRAMPOLINE(CalculateColumnLayoutEx) {
    static std::uintptr_t Callback(std::uintptr_t layout, std::uintptr_t drawInfo, bool force) {
        for (auto& panel : panels) {
            if (!panel.ready || !panel.visible || field<std::uintptr_t>(panel.owner, 40) != layout) continue;
            if (++panel.calculations == 1)
                Logging.Log("[material-columns] presentation calculator reached: LayoutEx groups=%u", static_cast<unsigned>(panel.count));
            positionPresentation(panel);
            horizontalMasks(panel, true);
            visible(panel.stockPane, false);
            visible(panel.pane, true);
            updateRows(panel, 0.f, true);
        }
        const auto result = Orig(layout, drawInfo, force);
#ifndef MATERIAL_COLUMNS_PANEL_HOST_TEST
        for (auto& panel : panels)
            if (panel.ready && panel.visible && field<std::uintptr_t>(panel.owner,40)==layout) traceOuterTiles(panel);
#endif
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(ColumnSlideSound) {
    static std::uintptr_t Callback(std::uintptr_t controller, bool forward) {
        // The coordinator emits one sound at the actual selection transition. The
        // private controller's interpolated page crossings must not emit duplicates.
        return forController(controller) ? 0 : Orig(controller, forward);
    }
};
HOOK_DEFINE_TRAMPOLINE(BindShortcutItem) {
    static std::uintptr_t Callback(std::uintptr_t element, std::uintptr_t slot,
                                   std::uint32_t logical, std::uintptr_t requestAddress) {
        const auto* request = reinterpret_cast<const BindingRequest*>(requestAddress);
        auto* panel = request ? forController(request->controller) : nullptr;
        if (auto* owner = outerOwner(slot); owner && !owner->outerBinding) return 0;
        if (!panel) {
            if (borrowedOwner(slot)) return 0; // Snapshot probes have their own stack token.
            return Orig(element, slot, logical, requestAddress);
        }
        if (!slot) return 0; // Inactive borrowed centers have no private owner.
        if (!request->page || field<std::uintptr_t>(request->page, 8) != request->controller) {
            Logging.Log("[material-columns] invalid binding request: control=%llx page=%llx logical=%u",
                static_cast<unsigned long long>(request->controller),
                static_cast<unsigned long long>(request->page), logical);
            clearSlot(slot);
            return 0;
        }
        if (logical % 3 == 0 && panel->ready && panel->visible) setCaption(*panel, request->page, logical / 3);
        std::uint32_t nativeIndex = 0;
        bool selected = false;
        if (!panel->ready || !panel->visible || !panel->bind ||
            !panel->bind(panel->context, logical, nativeIndex, selected)) {
            clearSlot(slot);
            return 0;
        }
        const auto stock = field<std::uintptr_t>(panel->owner, 424);
        const auto stockControl = stock ? field<std::uintptr_t>(stock, 80) : 0;
        const auto count = stockControl ? field<std::int32_t>(stockControl, 136) : 0;
        if (count < 1 || nativeIndex >= static_cast<std::uint32_t>(count - 1)) {
            Logging.Log("[material-columns] binding refused: native index=%u stock count=%d", nativeIndex, count);
            clearSlot(slot);
            return 0;
        }
        panel->binding = true;
        const bool priorBinding = memory::privateBinding(true);
        const auto result = Orig(element, slot, nativeIndex + 1, requestAddress);
        memory::privateBinding(priorBinding);
        function<void (*)(std::uintptr_t, int)>(0x11f6dcc)(slot, 1);
        function<void (*)(std::uintptr_t, bool, bool)>(0x1a584ec)(slot, selected && logical % 3 == 0, !outerOwner(slot));
        panel->binding = false;
        return result;
    }
};
void bindOuterTiles(Panel& panel) {
    panel.outerBinding = true;
    for (unsigned i = 0; i < panel.outer.size(); ++i) {
        auto& tile = panel.outer[i];
        if (!tile.page || tile.group < 0) continue;
        const auto logical = static_cast<unsigned>(tile.group) * 3 + i % 2 + 1;
        const auto elements = field<std::uintptr_t>(panel.controller, 112);
        if (logical >= field<unsigned>(panel.controller, 104)) continue;
        const auto element = field<std::uintptr_t>(elements, logical * 8);
        BindingRequest request{static_cast<int>(logical), 0, panel.controller, tile.page, tile.pane};
        BindShortcutItem::Callback(element, tile.slot, logical, reinterpret_cast<std::uintptr_t>(&request));
        field<std::uint8_t>(tile.pane, 89) = 255;
        visible(tile.pane, true);
    }
    panel.outerBinding = false;
}
HOOK_DEFINE_TRAMPOLINE(DestroyScreen) {
    static std::uintptr_t Callback(std::uintptr_t screen) {
        if (auto* panel = find(screen)) { parkOuterTiles(*panel, true); leaseCenters(*panel, false); }
        memory::forget(screen);
        forgetMaterialScreen(screen);
        if (auto* panel = find(screen)) *panel = {};
        return Orig(screen);
    }
};
} // namespace

bool installColumnPanel(std::uintptr_t mainBase, bool validateOnly) {
    if (installed) return true;
    if (!mainBase) {
        Logging.Log("[material-columns] panel hook refused: main base=0");
        return false;
    }
    constexpr totk::engine::CodeSite sites[] = {
        {0x123a220, 0xa9bb7bfd, 0xf9000bf9}, {0x1228418, 0xa9bd7bfd, 0xf9000bf5},
        {0x1acc644, 0xd10303ff, 0xfd002be9}, {0x1a7f090, 0xd102c3ff, 0xa9057bfd},
        {0x107f714, 0xa9bc7bfd, 0xa9015ff8},
        {0xf6e9c4, 0xd10283ff, 0xfd0023ea}, {0x1a34c94, 0xd10503ff, 0xa9117bfd},
        {0x7b6668, 0xd10183ff, 0xa9037bfd},
        {0xe744fc, 0xa9ba7bfd, 0xa9016ffc}, {0x1a584ec, 0xa9be7bfd, 0xa9014ff4},
        {0x22a8130, 0xa9bc7bfd, 0xa9015ff8},
        {0x6ff4e0, 0xa9bb7bfd, 0xf9000bf9}, {0x6ff27c, 0x39400028, 0x34000c28}, {0xf84cf0, 0xd10183ff, 0xa9037bfd},
        {0x1a58204, 0xa9be7bfd, 0xf9000bf3}, {0x2a65cf8, 0xb9404008, 0x7200003f}
    };
    if (!totk::engine::codeMatches(mainBase, sites, "material-columns", "PANEL_HOOK_REFUSED")) return false;
    if (validateOnly) return true;
    base = mainBase;
    CreateShortcutParts::InstallAtOffset(sites[0].offset);
    InitializeShortcutParts::InstallAtOffset(sites[1].offset);
    UpdateShortcutParts::InstallAtOffset(sites[2].offset);
    BindShortcutItem::InstallAtOffset(sites[3].offset);
    DestroyScreen::InstallAtOffset(sites[4].offset);
    UpdateColumnController::InstallAtOffset(sites[5].offset);
    ColumnSlideSound::InstallAtOffset(sites[6].offset);
    CalculateColumnLayoutEx::InstallAtOffset(sites[7].offset);
    InitializeColumnPage::InstallAtOffset(sites[8].offset);
    SelectBorrowedSlot::InstallAtOffset(sites[9].offset);
    CopyColumnRowLayout::InstallAtOffset(sites[10].offset);
    installed = true;
    return true;
}
bool columnPanelReady(std::uintptr_t screen) {
    const auto* panel = find(screen);
    return panel && panel->ready;
}
bool retainPooledSlot(std::uintptr_t slot, const void* parameters) {
    auto* panel = outerOwner(slot);
    if (!panel || !parameters) return false;
    const SlotPresentation presentation(parameters);
    for (auto& tile : panel->outer) if (tile.slot == slot) {
        if (tile.contentBound && tile.presentation == presentation) {
            ++panel->poolRetained;
            return true;
        }
        tile.presentation = presentation;
        tile.contentBound = true;
        ++panel->poolUpdates;
        return false;
    }
    return false;
}
bool configureColumnPanel(std::uintptr_t screen, std::size_t count, std::size_t focusedColumn,
                          BindColumnItem bind, void* context, DescribeColumn describe) {
    auto* panel = find(screen);
    if (!panel || !panel->ready || !bind || count > maximumColumns ||
        (count && focusedColumn >= count) || field<std::uint32_t>(screen, 440) != 3) {
        Logging.Log("[material-columns] panel configuration refused: count=%u focus=%u ready=%u",
                    static_cast<unsigned>(count), static_cast<unsigned>(focusedColumn),
                    static_cast<unsigned>(panel && panel->ready));
        return false;
    }
    const bool entering = !panel->visible;
    if (entering) {
        panel->stockWasVisible = (field<std::uint8_t>(panel->stockPane, 88) & 1) != 0;
        panel->vertical.fill(0);
        const auto parent = field<std::uintptr_t>(screen, 40);
        using Lookup = std::uintptr_t (*)(std::uintptr_t, const char*);
        if (const auto guide = function<Lookup>(0x7012ec)(parent, "N_Guide_00")) panel->guideY = field<float>(guide, 52);
        if (const auto sort = function<Lookup>(0x7012ec)(parent, "Pa_SortWindow_00")) panel->sortY = field<float>(sort, 52);
    } else if (count != panel->count) panel->vertical.fill(0);
    panel->bind = bind;
    panel->context = context;
    panel->describe = describe;
    panel->count = count;
    panel->visible = true;
    leaseCenters(*panel, true);
    for (const auto capture : panel->bands) memory::captureEnabled(capture, true);
    function<void (*)(std::uintptr_t, int)>(0x1a8bf4c)(panel->controller, static_cast<int>(count * 3));
    field<std::int32_t>(panel->controller, 140) = count ? static_cast<int>(focusedColumn) : 0;
    if (entering) field<std::int32_t>(panel->controller, 152) = count ? static_cast<int>(focusedColumn) : 0;
    field<std::uint8_t>(panel->controller, 190) |= 0x62;
    function<void (*)(std::uintptr_t)>(0x1a36180)(panel->controller);
    synchronizeAnimations(*panel);
    visible(panel->stockPane, false);
    visible(panel->pane, true);
    horizontalMasks(*panel, true);
    return true;
}
void animateColumnMove(std::uintptr_t screen, std::size_t group, int direction) {
    auto* panel = find(screen);
    if (panel && panel->visible && group < panel->count)
        panel->vertical[group] -= static_cast<float>(direction) * rowPitch;
}
void hideColumnPanel(std::uintptr_t screen) {
    auto* panel = find(screen);
    if (!panel || !panel->visible) return;
    panel->visible = false;
    parkOuterTiles(*panel);
    leaseCenters(*panel, false);
    for (const auto capture : panel->bands) memory::captureEnabled(capture, false);
    visible(panel->pane, false);
    visible(panel->stockPane, panel->stockWasVisible);
    horizontalMasks(*panel, false);
    hideVerticalMasks(*panel);
    const auto parent = field<std::uintptr_t>(screen, 40);
    using Lookup = std::uintptr_t (*)(std::uintptr_t, const char*);
    for (const auto& position : {std::pair{"N_Guide_00", panel->guideY}, std::pair{"Pa_SortWindow_00", panel->sortY}}) {
        if (const auto pane = function<Lookup>(0x7012ec)(parent, position.first)) {
            field<float>(pane, 52) = position.second; field<std::uint8_t>(pane, 88) |= 0x10;
        }
    }
    function<void (*)(std::uintptr_t, int)>(0x1a8bf4c)(panel->controller, 0);
    memory::menuClosed();
    static unsigned reports = 12;
    if (reports) {
        --reports;
        Logging.Log("[material-columns] memory pooled-content updates=%u retained=%u cache-bytes=%u",
            panel->poolUpdates, panel->poolRetained, unsigned(sizeof(SlotPresentation) * panel->outer.size()));
    }
    panel->poolUpdates = panel->poolRetained = 0;
    pumpDiagnostics(true);
    panel->bind = nullptr;
    panel->context = nullptr;
}
bool focusNativeItem(std::uintptr_t screen, std::uint32_t nativeIndex, bool refreshCaption) {
    auto* panel = find(screen);
    if (!panel || !panel->ready) return false;
    const auto stock = field<std::uintptr_t>(screen, 424);
    const auto controller = stock ? field<std::uintptr_t>(stock, 80) : 0;
    const auto count = controller ? field<std::int32_t>(controller, 136) : 0;
    if (count < 1 || nativeIndex >= static_cast<std::uint32_t>(count - 1) ||
        field<std::int32_t>(controller, 88) < 1) return false;
    const auto old = field<std::int32_t>(controller, 140);
    const auto next = static_cast<std::int32_t>(nativeIndex + 1);
    field<std::int32_t>(controller, 140) = next;
    // Stock pages are hidden, but their native action/check state still needs the correct page.
    field<std::int32_t>(controller, 152) = next;
    if (old == next && !refreshCaption) return true;
    field<std::uint8_t>(controller, 190) |= 0x62;
    using Focus = void (*)(std::uintptr_t, int, int, bool);
    function<Focus>(0x1acd2f8)(stock, old < 0 ? 0 : old, next, true);
    if (panel->visible) {
        // Reuse the verified caption resolver, retaining the stock single-row controller.
        // Only its two caption destinations are substituted during this synchronous call.
        const auto layout = field<std::uintptr_t>(stock, 32);
        const auto grip = field<std::uintptr_t>(stock, 128);
        field<std::uintptr_t>(stock, 32) = panel->layout;
        field<std::uintptr_t>(stock, 128) = field<std::uintptr_t>(panel->child, 128);
        function<Focus>(0x1acd2f8)(stock, next, next, true);
        field<std::uintptr_t>(stock, 32) = layout;
        field<std::uintptr_t>(stock, 128) = grip;
    }
    return true;
}
void playColumnMoveSound(std::uintptr_t screen, bool forward) {
    const auto* panel = find(screen);
    if (!panel || !panel->ready) return;
    const auto stock = field<std::uintptr_t>(screen, 424);
    if (stock) function<void (*)(std::uintptr_t, bool)>(0x1a34c94)(field<std::uintptr_t>(stock, 80), forward);
}
} // namespace material_columns::native
