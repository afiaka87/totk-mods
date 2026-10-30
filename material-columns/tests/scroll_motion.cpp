// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/pure/ScrollMotion.hpp"
#include "../src/pure/ColumnSelection.hpp"
#include <cassert>
using namespace material_columns;
int main() {
    assert(std::abs(scrollToward(-180, 0, 1, .3f, .1f) + 126) < .0001f);
    const auto half = scrollToward(-180, 0, .5f, .3f, .1f);
    assert(std::abs(scrollToward(half, 0, .5f, .3f, .1f) + 126) < .001f);
    assert(scrollToward(-180, 0, 0, .3f, .1f) == -180);
    assert(scrollToward(-.05f, 0, 1, .3f, .1f) == 0);
    assert(scrollToward(180, 0, 1, 1, .1f) == 0);
    float position = -180;
    for (int i = 0; i < 100; ++i) {
        const auto next = scrollToward(position, 0, 1, .3f, .1f);
        assert(next >= position && next <= 0);
        position = next;
    }
    assert(position == 0);
    assert(columnLabelOpacity(0)==1);
    assert(columnLabelOpacity(170)==.85f && columnLabelOpacity(-170)==.85f);
    assert(columnLabelOpacity(255)==0 && columnLabelOpacity(340)==0);
    for(float x=0;x<340;++x) {
        assert(columnLabelOpacity(x)==columnLabelOpacity(-x));
        assert(columnLabelOpacity(x+1)<=columnLabelOpacity(x));
    }
    assert(outerColumnOpacity(0)==1);
    assert(outerColumnOpacity(170)==.5f && outerColumnOpacity(-170)==.5f);
    assert(outerColumnOpacity(255)==0 && outerColumnOpacity(-255)==0);
    assert(outerColumnOpacity(340)==0);
    for(float x=0; x<340; x+=1) {
        assert(outerColumnOpacity(x)==outerColumnOpacity(-x));
        assert(outerColumnOpacity(x+1)<=outerColumnOpacity(x));
    }
    for (std::size_t count : {1u, 2u, 3u, 4u, 20u}) for (std::size_t i = 0; i < count; ++i) {
        const auto view = window(i, count);
        assert(view.overflow_above == (i > 1));
        assert(view.overflow_below == (count - i > 2));
        assert(rowOpacity(0) == 1);
        assert(rowOpacity(180) == 1);
        assert(rowOpacity(-180) == 1);
        assert(rowOpacity(360) == 0);
        assert(rowOpacity(-360) == 0);
    }
}
