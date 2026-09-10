#pragma once

#include <cstdint>

#include "RecallAnimationPolicy.hpp"

namespace self_recall::anim {


using self_recall::pure::kKindNone;
using self_recall::pure::kKindMove;
using self_recall::pure::kKindClimbMove;
using self_recall::pure::kKindClimbWait;
using self_recall::pure::kKindGlide;
using self_recall::pure::kKindFall;
using self_recall::pure::kKindParasailGlide;
using self_recall::pure::kInvalidSlot;

struct Sample {
    float frame;
    float rate;
    std::uint8_t kind;
    std::uint8_t slot;
};

void initialize(std::uintptr_t mainBase);

Sample capture(void* playerActor);

void beginRewind(void* playerActor);
void drive(void* playerActor, std::uint8_t sampleKind,
           std::uint8_t sampleSlot);
void release(void* playerActor, const char* reason);
void abandon();

}  // namespace self_recall::anim
