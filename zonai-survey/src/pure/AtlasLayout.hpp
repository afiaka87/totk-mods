// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>

namespace zonai_survey::atlas {
inline constexpr unsigned kAsciiCount = 96, kIconCount = 29;
inline constexpr unsigned kBatchQuads = 128, kSlotBytes = 65536, kSlotCount = 2;
struct alignas(16) Quad {
    float rect[4];
    std::uint32_t tile[4]; // Byte offset, width, height, RGBA8.
};
static_assert(sizeof(Quad) == 32);
inline constexpr unsigned kMaxQuads = kSlotBytes / sizeof(Quad);
inline constexpr unsigned asciiOffset(unsigned c) { return c==0x2019 ? 95*256 : ((c >= 32 && c <= 126 ? c : '?') - 32)*256; }
inline unsigned nextCharacter(const char*& p) {
    const auto c=static_cast<unsigned char>(*p++);
    if (c==0xe2 && static_cast<unsigned char>(p[0])==0x80 && static_cast<unsigned char>(p[1])==0x99) {
        p+=2; return 0x2019;
    }
    return c;
}
inline constexpr unsigned iconOffset(unsigned icon) { return kAsciiCount*256 + (icon < kIconCount ? icon : 0)*1024; }
inline unsigned rgba(float r, float g, float b, float a) {
    const auto byte = [](float f) { return unsigned(!(f>0) ? 0 : f>=1 ? 255 : f*255+0.5f); };
    return byte(r) | byte(g)<<8 | byte(b)<<16 | byte(a)<<24;
}
inline bool project(const float* v, const float* p, float x, float y, float z, float& sx, float& sy) {
    const float cx=v[0]*x+v[1]*y+v[2]*z+v[3], cy=v[4]*x+v[5]*y+v[6]*z+v[7];
    const float cz=v[8]*x+v[9]*y+v[10]*z+v[11];
    const float w=p[12]*cx+p[13]*cy+p[14]*cz+p[15];
    if (!std::isfinite(w) || w<0.0001f) return false;
    const float nx=(p[0]*cx+p[1]*cy+p[2]*cz+p[3])/w;
    const float ny=(p[4]*cx+p[5]*cy+p[6]*cz+p[7])/w;
    if (!std::isfinite(nx) || !std::isfinite(ny) || nx < -1 || nx > 1 || ny < -1 || ny > 1) return false;
    sx=(nx*0.5f+0.5f)*1280; sy=(0.5f-ny*0.5f)*720;
    return true;
}
// One game-thread producer, one render-thread consumer. Neither overwrites a reader's slot.
template<class T> class Mailbox {
    T slots_[3]{};
    unsigned back_=2, front_=0;
    std::atomic<unsigned> middle_{1};
public:
    void publish(const T& value) {
        slots_[back_]=value;
        back_=middle_.exchange(back_|4, std::memory_order_acq_rel)&3;
    }
    const T& consume() {
        if (middle_.load(std::memory_order_acquire)&4)
            front_=middle_.exchange(front_, std::memory_order_acq_rel)&3;
        return slots_[front_];
    }
};
}
