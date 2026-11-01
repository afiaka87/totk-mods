#pragma once

#include "../../../pure/RemoteTarget.hpp"

namespace zonai_ascend::remote {

struct Bindings {
    bool (*queryLayers)(Layers*, const Vec3*) = nullptr;
    bool (*blockedExit)(void* query, const Vec3*) = nullptr;
    bool (*isUnderground)(void* query) = nullptr;
    void* (*getActor)(void* node) = nullptr;
    void* (*getPlayer)(void* node) = nullptr;
    void* (*getSelectorChild)(void* selector) = nullptr;
    bool (*snapFloor)(void* actor, const Vec3*, Vec3*) = nullptr;
};

void bind(Bindings bindings);
void beginQuery(void* manager);
void commit(void* manager);
bool tryCeiling(void* manager, void* query, std::uint16_t reasons);
bool resolveFloor(void* manager, void* query, const Vec3* origin, Vec3* output,
                  bool* failed, bool nativePassed);
const void* markerPosition(void* manager, void* actor, const void* original);
bool cancelUnsafeArrival(void* node);
void finishApproachCancel(void* selector);

}
