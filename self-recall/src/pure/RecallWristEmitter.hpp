#pragma once
#include <array>
#include "RecallWristProvider.hpp"

namespace self_recall::pure {

inline bool relativeEffectMatrix(const float wrist[12], const float effect[12], float local[12]) {
    for (unsigned i = 0; i < 12; ++i)
        if (!std::isfinite(wrist[i]) || !std::isfinite(effect[i])) return false;
    const double a = wrist[0], b = wrist[1], c = wrist[2];
    const double d = wrist[4], e = wrist[5], f = wrist[6];
    const double g = wrist[8], h = wrist[9], j = wrist[10];
    const double det = a * (e*j-f*h) - b * (d*j-f*g) + c * (d*h-e*g);
    if (!std::isfinite(det) || std::abs(det) < 1e-10) return false;
    const double inverse[9]{(e*j-f*h)/det, (c*h-b*j)/det, (b*f-c*e)/det,
                            (f*g-d*j)/det, (a*j-c*g)/det, (c*d-a*f)/det,
                            (d*h-e*g)/det, (b*g-a*h)/det, (a*e-b*d)/det};
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned col = 0; col < 4; ++col) {
            double value = 0;
            for (unsigned k = 0; k < 3; ++k)
                value += inverse[row * 3 + k] * (double(effect[k * 4 + col]) -
                         (col == 3 ? wrist[k * 4 + 3] : 0));
            local[row * 4 + col] = float(value);
            if (!std::isfinite(local[row * 4 + col])) return false;
        }
    }
    return true;
}

inline bool emitterMatrix(const float wrist[12], const float local[12],
                          const float origin[3], float columns[16]) {
    for (unsigned col = 0; col < 4; ++col) {
        for (unsigned row = 0; row < 3; ++row) {
            double value = col == 3 ? double(wrist[row * 4 + 3]) + origin[row] : 0;
            for (unsigned k = 0; k < 3; ++k)
                value += double(wrist[row * 4 + k]) * local[k * 4 + col];
            columns[col * 4 + row] = float(value);
            if (!std::isfinite(columns[col * 4 + row])) return false;
        }
        columns[col * 4 + 3] = 0; // Native ELink's padded column layout.
    }
    return true;
}

struct WristEmitterFrame {
    std::uintptr_t emitterSet = 0;
    std::uint32_t instance = 0;
    WristEffectOwner owner{};
    float local[12]{};
};

template<std::size_t Capacity = 16>
class WristEmitterFrames {
    std::atomic_flag gate_ = ATOMIC_FLAG_INIT;
    std::array<WristEmitterFrame, Capacity> frames_{};
    struct Lock {
        std::atomic_flag& gate;
        explicit Lock(std::atomic_flag& g) : gate(g) { while (gate.test_and_set(std::memory_order_acquire)) {} }
        ~Lock() { gate.clear(std::memory_order_release); }
    };
public:
    void clear() { Lock lock(gate_); frames_ = {}; }
    bool put(const WristEmitterFrame& frame) {
        if (!frame.emitterSet || !frame.owner) return false;
        Lock lock(gate_);
        WristEmitterFrame* free = nullptr;
        for (auto& entry : frames_) {
            if (entry.emitterSet == frame.emitterSet) { entry = frame; return true; }
            if (!entry.emitterSet) free = &entry;
        }
        if (!free) return false;
        *free = frame;
        return true;
    }
    WristEmitterFrame get(std::uintptr_t set, std::uint32_t instance) {
        Lock lock(gate_);
        for (const auto& entry : frames_)
            if (set && entry.emitterSet == set && entry.instance == instance) return entry;
        return {};
    }
};
}
