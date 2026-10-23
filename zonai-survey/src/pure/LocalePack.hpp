// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace zonai_survey::locale {
inline constexpr unsigned kArenaBytes=163840, kCommonBytes=54272, kActorCount=2053;
inline constexpr unsigned kPackedTile=1u<<19;
inline std::uint32_t u32(const unsigned char* p) {
    return unsigned(p[0]) | unsigned(p[1])<<8 | unsigned(p[2])<<16 | unsigned(p[3])<<24;
}
inline unsigned u16(const unsigned char* p) { return unsigned(p[0]) | unsigned(p[1])<<8; }
inline std::uint32_t checksum(const unsigned char* p,unsigned size) {
    std::uint32_t crc=~0u;
    for(unsigned i=0;i<size;++i) {
        crc^=(i>=12 && i<16) ? 0 : p[i];
        for(unsigned b=0;b<8;++b) crc=(crc>>1) ^ (0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
// Pack storage is immutable after validation. No allocation or decoded-string cache.
class Pack {
    const unsigned char* data_{};
public:
    bool validate(const unsigned char* p,unsigned size,std::uint32_t catalog,const char* locale) {
        data_=nullptr;
        if(size<256 || size>kArenaBytes || std::memcmp(p,"ZSL1",4) || u32(p+4)!=1 ||
           u32(p+8)!=size || u32(p+16)!=catalog || u32(p+24)!=kActorCount ||
           std::memcmp(p+72,locale,4) || checksum(p,size)!=u32(p+12)) return false;
        const unsigned glyphs=u32(p+20),bits=u32(p+28),names=u32(p+32),bytes=u32(p+36);
        if(glyphs<96 || glyphs>=16384 || (bits!=4 && bits!=8) || bytes>=65535) return false;
        const unsigned extra=(glyphs-96)*(bits==4 ? 128 : 256), first=extra<65536 ? extra : 65536;
        if(extra>131072 || u32(p+48)!=256 || u32(p+52)!=kCommonBytes ||
           u32(p+56)!=256+kCommonBytes || u32(p+60)!=first ||
           u32(p+64)!=256+kCommonBytes+first || u32(p+68)!=extra-first ||
           u32(p+44)!=256+kCommonBytes+extra ||
           u32(p+40)!=u32(p+44)+glyphs*2 || names!=u32(p+40)+kActorCount*2 ||
           names>size || bytes!=size-names) return false;
        for(unsigned i=0;i<glyphs;++i) if(u16(p+u32(p+44)+i*2)>2048) return false;
        for(unsigned i=0;i<kActorCount;++i) {
            unsigned at=u16(p+u32(p+40)+i*2);
            if(at==65535) continue;
            if(at>=bytes || (at && p[names+at-1]!=0) || !p[names+at]) return false;
            bool terminated=false;
            while(at<bytes) {
                unsigned id=p[names+at++];
                if(!id) { terminated=true; break; }
                if(id&128) {
                    if(at==bytes || !p[names+at] || p[names+at]>=128) return false;
                    id=(id&127) | unsigned(p[names+at++])<<7;
                }
                if(id>glyphs) return false;
            }
            if(!terminated) return false;
        }
        data_=p; return true;
    }
    const char* name(unsigned actor) const {
        if(!data_ || actor>=kActorCount) return nullptr;
        const unsigned at=u16(data_+u32(data_+40)+actor*2);
        return at==65535 ? nullptr : reinterpret_cast<const char*>(data_+u32(data_+32)+at);
    }
    static unsigned next(const char*& p) {
        unsigned id=static_cast<unsigned char>(*p++);
        if(id&128) id=(id&127) | unsigned(static_cast<unsigned char>(*p++))<<7;
        return id;
    }
    float advance(unsigned id) const { return u16(data_+u32(data_+44)+(id-1)*2)/64.f; }
    unsigned tile(unsigned id) const {
        if(id<=96) return (id-1)*256;
        const bool packed=u32(data_+28)==4;
        const unsigned offset=(id-97)*(packed ? 128 : 256);
        return (offset&65535u) | ((1+offset/65536)<<16) | (packed ? kPackedTile : 0);
    }
    unsigned bankOffset(unsigned bank) const { return u32(data_+48+bank*8); }
    unsigned bankBytes(unsigned bank) const { return u32(data_+52+bank*8); }
    unsigned gpuBytes() const { return (bankOffset(2)+bankBytes(2)+4095)&~4095u; }
};

inline constexpr const char* kLocales[]{"JPja","USen","USes","USfr","USpt","EUen","EUes","EUfr",
                                      "EUde","EUit","EUpt","EUnl","EUru","KRko","CNzh","TWzh"};
inline unsigned desiredLocale(const char* code) {
    constexpr const char* codes[]{"ja","en-US","es-419","fr-CA","pt-BR","en-GB","es","fr",
                                  "de","it","pt","nl","ru","ko","zh-Hans","zh-Hant"};
    for(unsigned i=0;i<16;++i) if(std::strncmp(code,codes[i],8)==0) return i;
    return 0; // Matches the native unrecognized-code region/language fallback.
}
inline unsigned region(unsigned locale) {
    return locale==0 ? 0 : locale<=4 ? 1 : locale<=12 ? 2 : locale-10;
}
// The first line of the native RegionLangMask determines text, independent of voice settings.
inline unsigned selectedLocale(unsigned wanted,const char* mask,unsigned size) {
    unsigned allowed=0;
    for(unsigned at=0;at<size && mask[at]!='\n' && mask[at]!='\r';) {
        if(mask[at]==' ' || mask[at]=='\t') { ++at; continue; }
        unsigned end=at;
        while(end<size && mask[end]!=' ' && mask[end]!='\t' && mask[end]!='\r' && mask[end]!='\n') ++end;
        if(end-at==4) for(unsigned i=0;i<16;++i) if(!std::memcmp(mask+at,kLocales[i],4)) allowed|=1u<<i;
        at=end;
    }
    if(!allowed) return 16;
    if(allowed&(1u<<wanted)) return wanted;
    for(unsigned i=0;i<16;++i) if((allowed&(1u<<i)) && region(i)==region(wanted)) return i;
    for(unsigned i=0;i<16;++i) if(allowed&(1u<<i)) return i;
    return 16;
}
}
