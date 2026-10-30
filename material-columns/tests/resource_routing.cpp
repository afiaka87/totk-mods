// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/pure/ResourceRouting.hpp"
#include <cassert>
#include <cstring>

int main() {
    using namespace material_columns::resources;
    assert(layoutFor(layoutType, "ShortCut_00.bflyt") == LayoutId::Parent);
    assert(layoutFor(layoutType, "MCColumns_00.bflyt") == LayoutId::Columns);
    assert(layoutFor(layoutType, "PaShortCutSortWindow_00.bflyt") == LayoutId::Sort);
    for (auto name : {"PaShortCutSlideList_00.bflyt", "Other.bflyt", "ShortCut_00",
                      "ShortCut_00.bflyt.extra", "folder/ShortCut_00.bflyt"})
        assert(layoutFor(layoutType, name) == LayoutId::None);
    assert(layoutFor(0x616e696d, "ShortCut_00.bflyt") == LayoutId::None);
    assert(std::strcmp(animationLayout("MCColumns_00"), "PaShortCutSlideList_00") == 0);
    for (const char* name : {"MCColumns_001", "MCColumns_00_In.bflan", "MCColumns_00.bflyt",
                             "PaShortCutSlideList_00", "folder/MCColumns_00", ""})
        assert(animationLayout(name) == name);
    assert(animationLayout(nullptr) == nullptr);
}
