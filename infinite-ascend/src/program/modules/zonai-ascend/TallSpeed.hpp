#pragma once

#include "../../../pure/TallSpeedPolicy.hpp"

namespace zonai_ascend::speed {

using GetPlayer = void* (*)(void* node);

void bind(GetPlayer getPlayer);
class ApproachScope {
    void* previous_;
public:
    explicit ApproachScope(void* node);
    ~ApproachScope();
    ApproachScope(const ApproachScope&) = delete;
    ApproachScope& operator=(const ApproachScope&) = delete;
};

float scaleForwardStep(void* player, float nativeStep, bool travelCall);
float scaleApproachLookahead(void* node, float nativeStep);
void extendBraking(void* node);

}
