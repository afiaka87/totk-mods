// SPDX-License-Identifier: MIT
#include <lib.hpp>
#include <nn/util.h>
#include <algorithm>
#include "AtlasRenderer.hpp"
#include "GlyphIndex.hpp"
#include "IconGlyphs.hpp"
#include "LabelAtlas.hpp"
#include "LabelPlacement.hpp"
#include "SurveyOptions.hpp"
#include <cstring>

namespace zonai_survey::render {
namespace {
atlas::Mailbox<GlyphFrame> g_frames;
struct Banner { char lines[2][96]{}; std::uint64_t until{}; };
atlas::Mailbox<Banner> g_banners;
const GlyphFrame* g_frame{};
const Banner* g_banner{};
static_assert(pure::kMaxGlyphs*(atlas::kLongestName+1)+190 <= atlas::kMaxQuads);
static_assert(pure::kMaxGlyphs<=atlas::kMaxLabels);
atlas::NamePlacement g_names;
std::uint64_t g_lastBuild{};
float advance(unsigned c) { return atlas::kAdvances[atlas::asciiOffset(c)/256]/64.f; }
float textWidth(const char* p) {
    float width=0; while(p && *p) width+=advance(atlas::nextCharacter(p)); return width+2;
}
void quad(atlas::Quad* out,unsigned& n,float x,float y,unsigned offset,unsigned size,unsigned color) {
    if (n<atlas::kMaxQuads) out[n++]={{x,y,16,16},{offset,size,size,color}};
}
void text(atlas::Quad* out,unsigned& n,const atlas::PixelGrid& grid,float x,float y,const char* p,unsigned color) {
    while(p && *p) {
        const unsigned c=atlas::nextCharacter(p);
        if(c!=' ' && n<atlas::kMaxQuads) out[n++]=atlas::letterQuad(grid,x,y,atlas::asciiOffset(c),color);
        x+=advance(c);
    }
}
// Visible labels, nearest first, each snapped once so its icon and name move together.
unsigned collectLabels(const GlyphFrame& frame,const float* view,const float* proj,const atlas::PixelGrid& grid,
                       atlas::Label* labels,unsigned* glyphOf,const char** names) {
    const unsigned count=frame.count<pure::kMaxGlyphs ? frame.count : pure::kMaxGlyphs;
    unsigned order[pure::kMaxGlyphs];
    for(unsigned i=0;i<count;++i) order[i]=i;
    for(unsigned i=1;i<count;++i) {
        const unsigned key=order[i]; unsigned j=i;
        while(j && frame.glyphs[order[j-1]].distanceSq>frame.glyphs[key].distanceSq) { order[j]=order[j-1]; --j; }
        order[j]=key;
    }
    unsigned shown=0;
    for(unsigned i=0;i<count;++i) {
        const auto& g=frame.glyphs[order[i]];
        if(!(g.alpha>0)) continue;
        float x{},y{};
        if(!atlas::project(view,proj,g.x,g.y+0.9f,g.z,x,y) || x<24 || x>1256 || y<16 || y>704) continue;
        x=grid.snapX(x); y=grid.snapY(y);
        names[shown]=pure::glyphDisplayName(g.name);
        labels[shown]={g.name,g.x,g.z,g.distanceSq,{x,y,x+16,y+20},names[shown] ? x+19+textWidth(names[shown]) : 0};
        glyphOf[shown++]=order[i];
    }
    return shown;
}
float secondsSinceLastBuild() {
    const auto now=svcGetSystemTick();
    const float seconds=g_lastBuild ? float(now-g_lastBuild)/pure::kSystemTicksPerSecond : 0;
    g_lastBuild=now;
    return seconds;
}
}
void publishGlyphs(const GlyphFrame& frame) { g_frames.publish(frame); }
void clearGlyphs() { g_frames.publish(GlyphFrame{}); }
void atlasBanner(const char* first,const char* second,unsigned ticks) {
    Banner b;
    if(first) nn::util::SNPrintf(b.lines[0],sizeof(b.lines[0]),"%s",first);
    if(second) nn::util::SNPrintf(b.lines[1],sizeof(b.lines[1]),"%s",second);
    b.until=svcGetSystemTick()+std::uint64_t(ticks)*320000;
    g_banners.publish(b);
}
bool atlasHasContent() {
    g_frame=&g_frames.consume(); g_banner=&g_banners.consume();
    return g_frame->count || g_banner->until>svcGetSystemTick();
}
unsigned buildAtlasQuads(const float* view,const float* proj,const atlas::PixelGrid& grid,atlas::Quad* out) {
    unsigned n=0;
    const auto& frame=*g_frame;
    atlas::Label labels[pure::kMaxGlyphs];
    unsigned glyphOf[pure::kMaxGlyphs];
    const char* names[pure::kMaxGlyphs];
    const unsigned shown=collectLabels(frame,view,proj,grid,labels,glyphOf,names);
    float nameAlpha[pure::kMaxGlyphs];
    g_names.place(labels,shown,secondsSinceLastBuild(),nameAlpha);
    for(unsigned k=0;k<shown;++k) {
        const auto& g=frame.glyphs[glyphOf[k]];
        const float x=labels[k].icon.x0, y=labels[k].icon.y0;
        const float distance=__builtin_sqrtf(g.distanceSq);
        const float dim=distance<=40 ? 1 : distance>=300 ? 0.35f : 1-(distance-40)/260*0.65f;
        const unsigned icon=(g.flags&glyphs::kFlagInChest) ? unsigned(icons::Icon::Chest) : g.icon;
        const auto tint=pure::glyphTint(icon);
        quad(out,n,x,y,atlas::iconOffset(icon),32,atlas::rgba(tint.r,tint.g,tint.b,g.alpha*dim));
        if(nameAlpha[k]>0) text(out,n,grid,x+grid.snapX(19),y,names[k],atlas::rgba(1,1,1,g.alpha*nameAlpha[k]));
    }
    if(g_banner->until>svcGetSystemTick()) {
        const auto width=std::max(textWidth(g_banner->lines[0]),textWidth(g_banner->lines[1]));
        const float x=grid.snapX(width<1216 ? float(1248-width) : 32);
        text(out,n,grid,x,grid.snapY(40),g_banner->lines[0],0xffffffff);
        text(out,n,grid,x,grid.snapY(60),g_banner->lines[1],0xffffffff);
    }
    return n;
}
}
