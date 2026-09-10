#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace self_recall::palette {

inline constexpr std::size_t kGpuPoolAlignment = 0x1000;
inline constexpr std::size_t kPaletteBackingBytes = 3 * 0x10000;

inline std::uint32_t uniformPoolFlags(std::uint32_t cpuUncachedMask,
                                      std::uint32_t gpuUncachedMask) {
    return ((cpuUncachedMask & 2u) ? 2u : 4u) |
           ((gpuUncachedMask & 2u) ? 0x10u : 0x20u);
}

struct GpuBufferApi {
    void* device = nullptr;
    std::uint32_t memoryPoolFlags = 0;
    void (*poolBuilderDefaults)(void*) = nullptr;
    void (*poolBuilderDevice)(void*, void*) = nullptr;
    void (*poolBuilderStorage)(void*, void*, std::size_t) = nullptr;
    void (*poolBuilderFlags)(void*, int) = nullptr;
    std::uint8_t (*poolInitialize)(void*, const void*) = nullptr;
    void (*poolFinalize)(void*) = nullptr;
    std::uint32_t (*poolFlags)(const void*) = nullptr;
    void (*builderDefaults)(void*) = nullptr;
    void (*builderDevice)(void*, void*) = nullptr;
    void (*builderStorage)(void*, void*, std::ptrdiff_t, std::size_t) = nullptr;
    std::uint8_t (*initialize)(void*, const void*) = nullptr;
    void (*finalize)(void*) = nullptr;
    void* (*map)(const void*) = nullptr;
    std::uint64_t (*address)(const void*) = nullptr;
    void (*flush)(const void*, std::ptrdiff_t, std::size_t) = nullptr;

    explicit operator bool() const {
        return device && memoryPoolFlags && poolBuilderDefaults && poolBuilderDevice &&
            poolBuilderStorage && poolBuilderFlags && poolInitialize && poolFinalize && poolFlags &&
            builderDefaults && builderDevice && builderStorage && initialize && finalize &&
            map && address && flush;
    }
};

enum class GpuBufferStatus : unsigned {
    Ready, NotReady, InvalidLayout, InvalidApi, AlreadyCreated, PoolInitFailed,
    InvalidBacking, GraphicsInitFailed, InvalidMapping, InvalidAddress,
    InvalidSlot, SlotInFlight, InvalidBytes, OverlappingBytes, InvalidPatch,
};

struct GpuWordPatch {
    std::uint32_t offset = 0;
    std::uint32_t value = 0;
};

struct GpuBufferLayout {
    std::uint32_t bytes = 0;
    std::uint32_t stride = 0;
    std::uint32_t total = 0;
    unsigned slots = 0;
};

inline GpuBufferLayout gpuBufferLayout(std::uint32_t bytes, unsigned slots) {
    if (!bytes || bytes > UINT16_MAX || !slots || slots > 3) return {};
    const auto stride = (bytes + 0xFFu) & ~0xFFu;
    return {bytes, stride, stride * slots, slots};
}

class NativeGpuBuffer {
public:
    NativeGpuBuffer() = default;
    NativeGpuBuffer(const NativeGpuBuffer&) = delete;
    NativeGpuBuffer& operator=(const NativeGpuBuffer&) = delete;
    NativeGpuBuffer(NativeGpuBuffer&&) = delete;
    NativeGpuBuffer& operator=(NativeGpuBuffer&&) = delete;

    GpuBufferStatus create(const GpuBufferApi& api, std::span<std::byte> backing,
                           std::uint32_t bytes, unsigned slots) {
        if (poolInitialized_) return GpuBufferStatus::AlreadyCreated;
        const auto layout = gpuBufferLayout(bytes, slots);
        if (!layout.total) return GpuBufferStatus::InvalidLayout;
        if (!api) return GpuBufferStatus::InvalidApi;
        const auto poolBytes = (layout.total + kGpuPoolAlignment - 1) & ~(kGpuPoolAlignment - 1);
        const auto storage = reinterpret_cast<std::uintptr_t>(backing.data());
        if (!storage || (storage & (kGpuPoolAlignment - 1)) || backing.size() < poolBytes ||
            storage > UINTPTR_MAX - poolBytes) return GpuBufferStatus::InvalidBacking;
        const auto requestedCpu = api.memoryPoolFlags & 7u;
        const auto requestedGpu = api.memoryPoolFlags & 0x38u;
        if ((requestedCpu != 2 && requestedCpu != 4) ||
            (requestedGpu != 0x10 && requestedGpu != 0x20) || (api.memoryPoolFlags & ~0x3Fu))
            return GpuBufferStatus::InvalidApi;
        api_ = api;
        alignas(8) std::array<std::byte, 0x40> poolBuilder{};
        api_.poolBuilderDefaults(poolBuilder.data());
        api_.poolBuilderDevice(poolBuilder.data(), api_.device);
        api_.poolBuilderFlags(poolBuilder.data(), static_cast<int>(api_.memoryPoolFlags));
        api_.poolBuilderStorage(poolBuilder.data(), backing.data(), poolBytes);
        if (!api_.poolInitialize(pool_.data(), poolBuilder.data()))
            return fail(GpuBufferStatus::PoolInitFailed);
        poolInitialized_ = true;
        if (api_.poolFlags(pool_.data()) != api_.memoryPoolFlags)
            return fail(GpuBufferStatus::InvalidMapping);
        cached_ = requestedCpu == 4;

        alignas(8) std::array<std::byte, 0x40> builder{};
        api_.builderDefaults(builder.data());
        api_.builderDevice(builder.data(), api_.device);
        api_.builderStorage(builder.data(), pool_.data(), 0, layout.total);
        if (!api_.initialize(buffer_.data(), builder.data()))
            return fail(GpuBufferStatus::GraphicsInitFailed);
        graphicsInitialized_ = true;
        const auto mapped = static_cast<std::byte*>(api_.map(buffer_.data()));
        if (!mapped || mapped != backing.data() ||
            reinterpret_cast<std::uintptr_t>(mapped) > UINTPTR_MAX - layout.total)
            return fail(GpuBufferStatus::InvalidMapping);
        const auto gpu = api_.address(buffer_.data());
        if (!gpu || (gpu & 0xFFu) || gpu > UINT64_MAX - layout.total)
            return fail(GpuBufferStatus::InvalidAddress);
        std::memset(mapped, 0, layout.total);
        if (cached_) api_.flush(buffer_.data(), 0, layout.total);
        mapped_ = mapped;
        gpu_ = gpu;
        layout_ = layout;
        return GpuBufferStatus::Ready;
    }

    GpuBufferStatus upload(unsigned slot, std::span<const std::byte> bytes) {
        if (mapped_ && bytes.size() != layout_.bytes) return GpuBufferStatus::InvalidBytes;
        return uploadPatched(slot, bytes, {});
    }

    GpuBufferStatus uploadPatched(unsigned slot, std::span<const std::byte> bytes,
                                  std::span<const GpuWordPatch> patches) {
        if (!mapped_) return GpuBufferStatus::NotReady;
        if (slot >= layout_.slots) return GpuBufferStatus::InvalidSlot;
        if (inFlight_[slot]) return GpuBufferStatus::SlotInFlight;
        if (bytes.empty() || bytes.size() > layout_.bytes || !bytes.data())
            return GpuBufferStatus::InvalidBytes;
        if (patches.size() > 2) return GpuBufferStatus::InvalidPatch;
        std::array<GpuWordPatch, 2> checked{};
        for (std::size_t i = 0; i < patches.size(); ++i) {
            checked[i] = patches[i];
            const auto offset = checked[i].offset;
            if ((offset & 3u) || bytes.size() < 4 || offset > bytes.size() - 4)
                return GpuBufferStatus::InvalidPatch;
            for (std::size_t j = 0; j < i; ++j)
                if (checked[j].offset == offset) return GpuBufferStatus::InvalidPatch;
        }
        const auto source = reinterpret_cast<std::uintptr_t>(bytes.data());
        const auto target = reinterpret_cast<std::uintptr_t>(mapped_);
        if ((source >= target && source - target < layout_.total) ||
            (source < target && target - source < bytes.size()))
            return GpuBufferStatus::OverlappingBytes;
        const auto offset = layout_.stride * slot;
        std::memcpy(mapped_ + offset, bytes.data(), bytes.size());
        for (std::size_t i = 0; i < patches.size(); ++i)
            write(mapped_ + offset, checked[i].offset, checked[i].value);
        std::memset(mapped_ + offset + bytes.size(), 0, layout_.stride - bytes.size());
        if (cached_) api_.flush(buffer_.data(), offset, layout_.stride);
        uploaded_[slot] = true;
        return GpuBufferStatus::Ready;
    }

    std::uint64_t addressForSubmission(unsigned slot) {
        if (!mapped_ || slot >= layout_.slots || !uploaded_[slot]) return 0;
        inFlight_[slot] = true;
        return gpu_ + layout_.stride * slot;
    }

    bool retireAfterGpuFence(unsigned slot) {
        if (!mapped_ || slot >= layout_.slots) return false;
        inFlight_[slot] = false;
        uploaded_[slot] = false;
        return true;
    }

    bool releaseUnused() {
        for (const auto busy : inFlight_) if (busy) return false;
        release();
        return true;
    }

    bool ready() const { return mapped_ != nullptr; }
    GpuBufferLayout layout() const { return layout_; }

private:
    template<class T> static void write(void* pointer, std::size_t offset, const T& value) {
        std::memcpy(static_cast<std::byte*>(pointer) + offset, &value, sizeof(value));
    }
    GpuBufferStatus fail(GpuBufferStatus status) { release(); return status; }
    void release() {
        if (graphicsInitialized_) api_.finalize(buffer_.data());
        if (poolInitialized_) api_.poolFinalize(pool_.data());
        graphicsInitialized_ = poolInitialized_ = false;
        cached_ = false;
        mapped_ = nullptr;
        gpu_ = 0;
        layout_ = {};
        uploaded_ = {};
        inFlight_ = {};
        pool_ = {};
        buffer_ = {};
    }

    alignas(8) std::array<std::byte, 0x100> pool_{};
    alignas(8) std::array<std::byte, 0x30> buffer_{};
    GpuBufferApi api_{};
    std::byte* mapped_ = nullptr;
    std::uint64_t gpu_ = 0;
    GpuBufferLayout layout_{};
    std::array<bool, 3> uploaded_{};
    std::array<bool, 3> inFlight_{};
    bool poolInitialized_ = false;
    bool graphicsInitialized_ = false;
    bool cached_ = false;
};

GpuBufferApi resolveGpuBufferApi(std::uintptr_t mainBase);

} // namespace self_recall::palette
