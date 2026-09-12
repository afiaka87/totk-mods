#pragma once

#include "RecallRender.hpp"

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
}

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

}

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

}

#include <cmath>

namespace self_recall::palette {

enum class MaterialStatus : unsigned {
    Ready, WrongModel, MissingMaterial, MissingShader, InvalidIndex,
    UnsupportedParameter, MissingSource, SourceRange, MissingMapping,
    UnmappedParameter, UniformRange, AliasedParameters, BufferUnavailable,
    NonfiniteSource, InvalidSaturation, InvalidBufferSize, OverlappingBuffers,
    PendingUpload, ValueMismatch,
};

struct ParameterLocation {
    std::uint32_t sourceOffset = 0;
    std::uint32_t uniformOffset = 0;
};

struct SceneMaterialView {
    const void* material = nullptr;
    const void* resource = nullptr;
    const void* nativeBuffer = nullptr;
    const std::byte* source = nullptr;
    std::uint32_t sourceBytes = 0;
    std::uint32_t uniformBytes = 0;
    unsigned bufferIndex = 0;
    unsigned bufferCount = 0;
    ParameterLocation character{};
    ParameterLocation other{};
};

namespace detail {
template<class T> inline T read(const void* base, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
    return value;
}
inline bool floatRange(std::uint32_t offset, std::uint32_t size) {
    return !(offset & 3u) && size >= sizeof(float) && offset <= size - sizeof(float);
}
}

inline MaterialStatus describeSceneMaterial(const void* model, std::uintptr_t mainBase,
        int materialIndex, int characterIndex, int otherIndex, SceneMaterialView& out) {
    using detail::read;
    if (!model || !mainBase || read<std::uintptr_t>(model, 0) != mainBase + 0x045C0570)
        return MaterialStatus::WrongModel;
    if (materialIndex != 0 || !read<std::uint16_t>(model, 0x16A))
        return MaterialStatus::MissingMaterial;
    const auto material = read<const void*>(model, 0x180);
    if (!material) return MaterialStatus::MissingMaterial;
    const auto resource = read<const void*>(material, 0);
    if (!resource) return MaterialStatus::MissingMaterial;
    const auto assignment = read<const void*>(resource, 0x10);
    if (!assignment) return MaterialStatus::MissingShader;
    const auto shader = read<const void*>(assignment, 0);
    if (!shader) return MaterialStatus::MissingShader;
    const auto count = read<std::uint16_t>(shader, 0x4A);
    const auto params = read<const std::byte*>(shader, 0x20);
    if (!params || characterIndex < 0 || otherIndex < 0 ||
        characterIndex >= count || otherIndex >= count)
        return MaterialStatus::InvalidIndex;
    if (characterIndex == otherIndex) return MaterialStatus::AliasedParameters;
    const auto offsets = read<const void*>(resource, 0x60);
    if (!offsets) return MaterialStatus::MissingMapping;
    SceneMaterialView candidate;
    candidate.material = material;
    candidate.resource = resource;
    candidate.source = read<const std::byte*>(material, 0x48);
    candidate.sourceBytes = read<std::uint16_t>(shader, 0x4C);
    const auto uniformBytes = read<std::uint64_t>(material, 0x60);
    if (!uniformBytes || uniformBytes > UINT16_MAX ||
        uniformBytes != read<std::uint16_t>(resource, 0xAA))
        return MaterialStatus::UniformRange;
    candidate.uniformBytes = static_cast<std::uint32_t>(uniformBytes);
    if (!candidate.source || !candidate.sourceBytes) return MaterialStatus::MissingSource;
    const int indices[]{characterIndex, otherIndex};
    ParameterLocation* locations[]{&candidate.character, &candidate.other};
    for (unsigned i = 0; i < 2; ++i) {
        const auto param = params + static_cast<std::size_t>(indices[i]) * 0x18;
        if (read<std::uintptr_t>(param, 0) || read<std::uint8_t>(param, 0x12) != 12)
            return MaterialStatus::UnsupportedParameter;
        const auto sourceOffset = read<std::uint16_t>(param, 0x10);
        if (!detail::floatRange(sourceOffset, candidate.sourceBytes)) return MaterialStatus::SourceRange;
        const auto uniformOffset = read<std::int32_t>(offsets, static_cast<std::size_t>(indices[i]) * 4);
        if (uniformOffset < 0) return MaterialStatus::UnmappedParameter;
        if (!detail::floatRange(static_cast<std::uint32_t>(uniformOffset), candidate.uniformBytes))
            return MaterialStatus::UniformRange;
        if (!std::isfinite(read<float>(candidate.source, sourceOffset))) return MaterialStatus::NonfiniteSource;
        *locations[i] = {sourceOffset, static_cast<std::uint32_t>(uniformOffset)};
    }
    if (candidate.character.sourceOffset == candidate.other.sourceOffset ||
        candidate.character.uniformOffset == candidate.other.uniformOffset)
        return MaterialStatus::AliasedParameters;
    candidate.bufferCount = read<std::uint8_t>(material, 0xA);
    candidate.bufferIndex = read<std::uint8_t>(model, 0x15) & 3u;
    const auto buffers = read<const std::byte*>(material, 0x40);
    if (!buffers || !(read<std::uint16_t>(material, 8) & 1u) ||
        !candidate.bufferCount || candidate.bufferCount > 3 ||
        candidate.bufferIndex >= candidate.bufferCount)
        return MaterialStatus::BufferUnavailable;
    candidate.nativeBuffer = buffers + 0x48 * candidate.bufferIndex;
    if (!read<std::uintptr_t>(candidate.nativeBuffer, 8)) return MaterialStatus::BufferUnavailable;
    out = candidate;
    return MaterialStatus::Ready;
}

struct Saturation { float character; float other; };

inline MaterialStatus verifyScenePaletteUpload(const SceneMaterialView& view,
        std::span<const std::byte> nativeUniform, Saturation expected) {
    if (!view.material || !view.source || !view.uniformBytes ||
        nativeUniform.size() != view.uniformBytes || !nativeUniform.data() ||
        !view.bufferCount || view.bufferCount > 3 || view.bufferIndex >= view.bufferCount)
        return MaterialStatus::InvalidBufferSize;
    if (!std::isfinite(expected.character) || !std::isfinite(expected.other) ||
        expected.character < 0 || expected.character > 1 ||
        expected.other < 0 || expected.other > 1) return MaterialStatus::InvalidSaturation;
    if (!detail::floatRange(view.character.sourceOffset, view.sourceBytes) ||
        !detail::floatRange(view.other.sourceOffset, view.sourceBytes) ||
        !detail::floatRange(view.character.uniformOffset, view.uniformBytes) ||
        !detail::floatRange(view.other.uniformOffset, view.uniformBytes)) return MaterialStatus::UniformRange;
    if ((detail::read<std::uint8_t>(view.material, 0x2C) & 1u) ||
        (detail::read<std::uint8_t>(view.material, 0x2D) & (1u << view.bufferIndex)))
        return MaterialStatus::PendingUpload;
    if (detail::read<float>(view.source, view.character.sourceOffset) != expected.character ||
        detail::read<float>(view.source, view.other.sourceOffset) != expected.other ||
        detail::read<float>(nativeUniform.data(), view.character.uniformOffset) != expected.character ||
        detail::read<float>(nativeUniform.data(), view.other.uniformOffset) != expected.other)
        return MaterialStatus::ValueMismatch;
    return MaterialStatus::Ready;
}

}

namespace self_recall::palette {

void install(std::uintptr_t mainBase);
void beginFrame(std::uint64_t epoch, std::uint32_t animationGeneration);
void afterNativeModel(const void* model);
std::uint64_t takeFailure();
const char* failureName(std::uint64_t failure);

}

#include "RecallPlayback.hpp"

namespace sead { class Heap; }

namespace self_recall::native_path {

void install(std::uintptr_t mainBase);
void initializeForScene(void* extension, void* scene, sead::Heap* heap);
void beginFrame(std::uint64_t epoch, std::uint32_t generation);
bool publish(const pure::NativePathRoute& route);
std::uint64_t takeFailure();

}
