// SPDX-License-Identifier: MIT
#include <lib.hpp>
#include <nn/util.h>
#include <algorithm>
#include "AtlasRenderer.hpp"
#include "GlyphIndex.hpp"
#include "IconGlyphs.hpp"
#include "LabelAtlas.hpp"
#include <cstring>

namespace zonai_survey::render {
namespace {
atlas::Mailbox<GlyphFrame> g_frames;
struct Banner { char lines[2][96]{}; std::uint64_t until{}; };
atlas::Mailbox<Banner> g_banners;
const GlyphFrame* g_frame{};
const Banner* g_banner{};
std::atomic<unsigned> g_drawn{}, g_offscreen{}, g_dropped{};
std::atomic<bool> g_probe{};
static_assert(pure::kMaxGlyphs*(atlas::kLongestName+1)+190 <= atlas::kMaxQuads);
struct Box { float x0,y0,x1,y1; };
bool overlap(const Box& a,const Box& b) { return !(a.x1<b.x0 || b.x1<a.x0 || a.y1<b.y0 || b.y1<a.y0); }
float advance(unsigned c) { return atlas::kAdvances[atlas::asciiOffset(c)/256]/64.f; }
float textWidth(const char* p) {
    float width=0; while(p && *p) width+=advance(atlas::nextCharacter(p)); return width+2;
}
void quad(atlas::Quad* out,unsigned& n,float x,float y,unsigned offset,unsigned size,unsigned color) {
    if (n<atlas::kMaxQuads) out[n++]={{x,y,16,16},{offset,size,size,color}};
}
void text(atlas::Quad* out,unsigned& n,float x,float y,const char* p,unsigned color) {
    while(p && *p) {
        const unsigned c=atlas::nextCharacter(p);
        if(c!=' ') quad(out,n,x,y,atlas::asciiOffset(c),16,color);
        x+=advance(c);
    }
}
}
void publishGlyphs(const GlyphFrame& frame) { g_frames.publish(frame); }
void clearGlyphs() { g_frames.publish(GlyphFrame{}); }
void setSymbolProbeVisible(bool value) { g_probe.store(value); }
bool symbolProbeVisible() { return g_probe.load(); }
std::uint32_t lastGlyphsDrawn() { return g_drawn.load(); }
std::uint32_t lastGlyphsOffscreen() { return g_offscreen.load(); }
std::uint32_t lastNamesDropped() { return g_dropped.load(); }
void atlasBanner(const char* first,const char* second,unsigned ticks) {
    Banner b;
    if(first) nn::util::SNPrintf(b.lines[0],sizeof(b.lines[0]),"%s",first);
    if(second) nn::util::SNPrintf(b.lines[1],sizeof(b.lines[1]),"%s",second);
    b.until=svcGetSystemTick()+std::uint64_t(ticks)*320000;
    g_banners.publish(b);
}
bool atlasHasContent() {
    g_frame=&g_frames.consume(); g_banner=&g_banners.consume();
    const bool content=g_frame->count || g_banner->until>svcGetSystemTick() || symbolProbeVisible();
    if(!content) { g_drawn=0; g_offscreen=0; g_dropped=0; }
    return content;
}
unsigned buildAtlasQuads(const float* view,const float* proj,atlas::Quad* out) {
    unsigned n=0, drawn=0, offscreen=0, dropped=0;
    const auto& frame=*g_frame;
    const unsigned count=frame.count<pure::kMaxGlyphs ? frame.count : pure::kMaxGlyphs;
    unsigned order[pure::kMaxGlyphs];
    for(unsigned i=0;i<count;++i) order[i]=i;
    for(unsigned i=1;i<count;++i) {
        const unsigned key=order[i]; unsigned j=i;
        while(j && frame.glyphs[order[j-1]].distanceSq>frame.glyphs[key].distanceSq) { order[j]=order[j-1]; --j; }
        order[j]=key;
    }
    Box boxes[pure::kMaxGlyphs]; unsigned boxCount=0;
    for(unsigned i=0;i<count;++i) {
        const auto& g=frame.glyphs[order[i]];
        if(!(g.alpha>0)) continue;
        float x{},y{};
        if(!atlas::project(view,proj,g.x,g.y+0.9f,g.z,x,y) || x<24 || x>1256 || y<16 || y>704) { ++offscreen; continue; }
        const char* name=pure::glyphDisplayName(g.name);
        Box b{x,y,x+16+(name ? 3+textWidth(name) : 0),y+20};
        for(unsigned j=0;name && j<boxCount;++j) if(overlap(b,boxes[j])) { name=nullptr; ++dropped; b.x1=x+16; }
        const float distance=__builtin_sqrtf(g.distanceSq);
        const float dim=distance<=40 ? 1 : distance>=300 ? 0.35f : 1-(distance-40)/260*0.65f;
        const float alpha=g.alpha*dim;
        const unsigned icon=(g.flags&glyphs::kFlagInChest) ? unsigned(icons::Icon::Chest) : g.icon;
        const auto tint=pure::glyphTint(icon);
        quad(out,n,x,y,atlas::iconOffset(icon),32,atlas::rgba(tint.r,tint.g,tint.b,alpha));
        if(name) text(out,n,x+19,y,name,atlas::rgba(0.96f,0.97f,1,alpha));
        boxes[boxCount++]=b; ++drawn;
    }
    if(g_banner->until>svcGetSystemTick()) {
        const auto width=std::max(textWidth(g_banner->lines[0]),textWidth(g_banner->lines[1]));
        const float x=width<1216 ? float(1248-width) : 32;
        text(out,n,x,40,g_banner->lines[0],0xffffffff);
        text(out,n,x,60,g_banner->lines[1],0xffffffff);
    }
    g_drawn=drawn; g_offscreen=offscreen; g_dropped=dropped;
    return n;
}
}
