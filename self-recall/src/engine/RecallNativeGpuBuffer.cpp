#include "RecallNativeGpuBuffer.hpp"

#include <nvn/nvn.h>

namespace self_recall::palette {
namespace {
static_assert(sizeof(NVNbufferBuilder) == 0x40);
static_assert(sizeof(NVNbuffer) == 0x30);
static_assert(sizeof(NVNmemoryPoolBuilder) == 0x40);
static_assert(sizeof(NVNmemoryPool) == 0x100);
static_assert(sizeof(NVNboolean) == 1);
static_assert(NVN_MEMORY_POOL_FLAGS_CPU_UNCACHED == 2 && NVN_MEMORY_POOL_FLAGS_CPU_CACHED == 4);
static_assert(NVN_MEMORY_POOL_FLAGS_GPU_UNCACHED == 0x10 && NVN_MEMORY_POOL_FLAGS_GPU_CACHED == 0x20);

template<class T> T read(std::uintptr_t address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}
template<class T> T dispatch(std::uintptr_t base, std::uintptr_t offset) {
    const auto slot = read<std::uintptr_t>(base + offset);
    return slot ? read<T>(slot) : T{};
}
} // namespace

GpuBufferApi resolveGpuBufferApi(std::uintptr_t mainBase) {
    GpuBufferApi api;
    if (!mainBase) return api;
    const auto graphics = dispatch<std::uintptr_t>(mainBase, 0x0462EF88);
    const auto policy = dispatch<std::uintptr_t>(mainBase, 0x04638520);
    if (!graphics || !policy) return api;
    api.device = read<void*>(graphics + 0x30);
    api.memoryPoolFlags = uniformPoolFlags(read<std::uint32_t>(policy + 0xC),
                                           read<std::uint32_t>(policy + 0x10));
    api.poolBuilderDefaults = dispatch<decltype(api.poolBuilderDefaults)>(mainBase, 0x04617388);
    api.poolBuilderDevice = dispatch<decltype(api.poolBuilderDevice)>(mainBase, 0x04617098);
    api.poolBuilderStorage = dispatch<decltype(api.poolBuilderStorage)>(mainBase, 0x04617390);
    api.poolBuilderFlags = dispatch<decltype(api.poolBuilderFlags)>(mainBase, 0x04617398);
    api.poolInitialize = dispatch<decltype(api.poolInitialize)>(mainBase, 0x046170A0);
    api.poolFinalize = dispatch<decltype(api.poolFinalize)>(mainBase, 0x046170A8);
    api.poolFlags = dispatch<decltype(api.poolFlags)>(mainBase, 0x04616EE8);
    api.builderDefaults = dispatch<decltype(api.builderDefaults)>(mainBase, 0x04616EC8);
    api.builderDevice = dispatch<decltype(api.builderDevice)>(mainBase, 0x04616ED0);
    api.builderStorage = dispatch<decltype(api.builderStorage)>(mainBase, 0x04616ED8);
    api.initialize = dispatch<decltype(api.initialize)>(mainBase, 0x04616EE0);
    api.finalize = dispatch<decltype(api.finalize)>(mainBase, 0x04616EF0);
    api.map = dispatch<decltype(api.map)>(mainBase, 0x04616EF8);
    api.address = dispatch<decltype(api.address)>(mainBase, 0x04616F10);
    api.flush = dispatch<decltype(api.flush)>(mainBase, 0x04616F00);
    return api ? api : GpuBufferApi{};
}
} // namespace self_recall::palette
