#pragma once

#include "RecallGpuLifetime.hpp"

namespace self_recall::gpu_lifetime {

namespace site {
inline constexpr std::uintptr_t kModelListBeginCall = 0x0096F38C;
inline constexpr std::uintptr_t kModelListBeginReturn = kModelListBeginCall + 4;
inline constexpr std::uintptr_t kModelListEndReturn = 0x0096F3E0;
inline constexpr std::uintptr_t kModelPoolClearReturn = 0x009770A0;
constexpr bool isModelListBegin(std::uintptr_t mainBase, std::uintptr_t caller) {
    return caller >= mainBase && caller - mainBase == kModelListBeginReturn;
}
inline constexpr std::uintptr_t kLayerListBeginCalls[]{
    0x8174C8, 0x8175C8, 0x817A90, 0x817BD0, 0x818088, 0x818564, 0x819000
};
constexpr bool isRenderListBegin(std::uintptr_t mainBase, std::uintptr_t caller) {
    if (isModelListBegin(mainBase, caller)) return true;
    if (caller < mainBase) return false;
    for (const auto pc : kLayerListBeginCalls) if (caller - mainBase == pc + 4) return true;
    return false;
}
} // namespace site

void install(std::uintptr_t mainBase);
void requestTracking();
bool bindingPhaseReady();

enum class Operation : unsigned {
    FrameBegin = 1, FrameSeal, Submit, Fence, PoolConfigure, ListBegin,
    ListEnd, PoolClear, ListCopy, PrivateBind, AliasReset, AliasCopy,
    DisplayCopy, DirectBegin, DirectEnd, FencePrepare
};

class Access {
public:
    Access();
    ~Access();
    Access(const Access&) = delete;
    Access& operator=(const Access&) = delete;
    pure::RecallGpuLifetime& ledger() const;
    unsigned noteFailure(Operation operation) const;
    unsigned failureOperation() const;
    void logBindingFailure(std::uintptr_t commandBuffer, unsigned slot) const;
};

} // namespace self_recall::gpu_lifetime
