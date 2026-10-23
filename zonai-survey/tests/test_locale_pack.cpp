// SPDX-License-Identifier: MIT
#include <doctest.h>
#include "LocalePack.hpp"
#include "AtlasLayout.hpp"
#include <array>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace zonai_survey::locale;
namespace {
void put(std::vector<unsigned char>& v,unsigned at,unsigned value) {
    for(unsigned i=0;i<4;++i) v[at+i]=static_cast<unsigned char>(value>>(i*8));
}
void seal(std::vector<unsigned char>& v) { put(v,12,checksum(v.data(),unsigned(v.size()))); }
std::vector<unsigned char> fixture() {
    const unsigned glyphs=800, extra=(glyphs-96)*128;
    const unsigned advances=256+kCommonBytes+extra, offsets=advances+glyphs*2,names=offsets+kActorCount*2;
    std::vector<unsigned char> v(names+4);
    std::memcpy(v.data(),"ZSL1",4);
    const unsigned fields[]{1,unsigned(v.size()),0,42,glyphs,kActorCount,4,names,4,offsets,advances,
                             256,kCommonBytes,256+kCommonBytes,65536,256+kCommonBytes+65536,extra-65536};
    for(unsigned i=0;i<17;++i) put(v,4+i*4,fields[i]);
    std::memcpy(v.data()+72,"TWzh",4);
    std::memset(v.data()+offsets,255,kActorCount*2);
    v[offsets]=v[offsets+1]=0;
    v[names]=2; v[names+1]=0xA0; v[names+2]=6; // Glyph IDs 2,800, terminator.
    seal(v); return v;
}
}
TEST_CASE("Text language follows native supported mask and region fallback, never voice") {
    constexpr char mask[]="JPja USen USes USfr EUen EUes EUfr EUde EUit EUnl EUru KRko CNzh TWzh\r\nNormal\r\n121";
    const char* codes[]{"ja","en-US","es-419","fr-CA","pt-BR","en-GB","es","fr","de","it","pt","nl","ru","ko","zh-Hans","zh-Hant"};
    for(unsigned i=0;i<16;++i) {
        CHECK(desiredLocale(codes[i])==i);
        CHECK(selectedLocale(i,mask,sizeof(mask))==(i==4 ? 1 : i==10 ? 5 : i));
    }
    CHECK(desiredLocale("unknown")==0);
    CHECK(selectedLocale(4,"USen USpt",9)==4);
    CHECK(selectedLocale(15,"JPja",4)==0);
    CHECK(selectedLocale(1,"",0)==16);
}
TEST_CASE("Locale pack validates bounds, checksums, IDs and missing actors without allocation") {
    auto v=fixture(); Pack p;
    REQUIRE(p.validate(v.data(),unsigned(v.size()),42,"TWzh"));
    CHECK(p.gpuBytes()==147456);
    CHECK(p.name(1)==nullptr); CHECK(p.name(kActorCount)==nullptr);
    auto name=p.name(0); REQUIRE(name!=nullptr);
    CHECK(Pack::next(name)==2); CHECK(Pack::next(name)==800); CHECK(*name==0);
    CHECK(p.tile(96)==95*256);
    for(unsigned id=97;id<=800;++id) {
        const unsigned tile=p.tile(id),offset=(id-97)*128,bank=(tile>>16)&3;
        CHECK(bank==1+offset/65536); CHECK((tile&65535)==offset%65536); CHECK((tile&kPackedTile)!=0);
        CHECK((tile&65535)+128<=p.bankBytes(bank));
    }
    CHECK_FALSE(p.validate(v.data(),255,42,"TWzh"));
    CHECK_FALSE(p.validate(v.data(),unsigned(v.size()),43,"TWzh"));
    CHECK_FALSE(p.validate(v.data(),unsigned(v.size()),42,"CNzh"));
    for(unsigned at=4;at<72;at+=4) {
        if(at==12) continue;
        auto bad=v; put(bad,at,0xffffffffu); seal(bad);
        CHECK_FALSE(p.validate(bad.data(),unsigned(bad.size()),42,"TWzh"));
    }
    auto bad=v; bad.back()=0x80; seal(bad);
    CHECK_FALSE(p.validate(bad.data(),unsigned(bad.size()),42,"TWzh"));
    bad=v; bad[u32(v.data()+32)+2]=127; seal(bad);
    CHECK_FALSE(p.validate(bad.data(),unsigned(bad.size()),42,"TWzh"));
    bad=v; bad[300]^=1;
    CHECK_FALSE(p.validate(bad.data(),unsigned(bad.size()),42,"TWzh"));
}
TEST_CASE("Long localized names are admitted whole while icons and banners retain capacity") {
    using namespace zonai_survey::atlas;
    unsigned budget=kMaxQuads-48-190,total=48+190;
    for(unsigned i=0;i<48;++i) {
        const unsigned needed=i%3 ? 56 : 78;
        const auto before=budget;
        if(admitName(needed,budget)) { total+=needed; CHECK(budget+needed==before); }
        else CHECK(budget==before);
        CHECK(total<=kMaxQuads);
    }
    CHECK_FALSE(admitName(kMaxQuads,budget));
}
#ifdef SURVEY_LOCALE_TEST_DIR
TEST_CASE("Every generated native locale pack validates and covers all named actors") {
    unsigned files=0;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(SURVEY_LOCALE_TEST_DIR)) {
        if(entry.path().extension()!=".bin") continue;
        std::ifstream stream(entry.path(),std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(stream)),{});
        REQUIRE(data.size()>=256);
        const auto language=entry.path().stem().string();
        Pack p; REQUIRE(p.validate(data.data(),unsigned(data.size()),u32(data.data()+16),language.c_str()));
        unsigned named=0;
        for(unsigned i=0;i<kActorCount;++i) if(const char* text=p.name(i)) {
            ++named; unsigned count=0;
            while(*text) { const auto id=Pack::next(text); CHECK(p.advance(id)>=0); CHECK(++count<512); }
        }
        CHECK(named==1870); ++files;
    }
    CHECK(files==59);
}
#endif
