// SPDX-License-Identifier: MIT
#pragma once
#include "AtlasLayout.hpp"
#include "GlyphRenderer.hpp"
namespace zonai_survey::render {
bool atlasHasContent();
unsigned buildAtlasQuads(const float* view, const float* projection, const atlas::PixelGrid& grid,
                         atlas::Quad* output);
void atlasBanner(const char* first, const char* second, unsigned ticks);
}
