#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

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
} // namespace detail

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

inline std::uint32_t fullColorCacheTag(std::uint32_t locations) {
    std::uint32_t tag = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const auto location = (locations >> shift) & 0xFFu;
        if (location != 0xFF && location > 0x7F) return 0;
        if (!tag && location < 0x7F) tag = 0x80u << shift;
    }
    return tag;
}

inline std::uint32_t sceneMaterialComparison(std::uint32_t cached,
        std::uint32_t locations, bool fullColor) {
    const auto tag = fullColor ? fullColorCacheTag(locations) : 0;
    if (!tag) return cached;
    return cached == (locations | tag) ? locations : ~locations;
}

inline MaterialStatus copyPaletteVariant(const SceneMaterialView& view,
        std::span<const std::byte> nativeUniform, std::span<std::byte> destination,
        Saturation saturation) {
    if (!std::isfinite(saturation.character) || !std::isfinite(saturation.other) ||
        saturation.character < 0 || saturation.character > 1 ||
        saturation.other < 0 || saturation.other > 1) return MaterialStatus::InvalidSaturation;
    if (!view.uniformBytes || nativeUniform.size() != view.uniformBytes ||
        destination.size() != view.uniformBytes) return MaterialStatus::InvalidBufferSize;
    if (!detail::floatRange(view.character.uniformOffset, view.uniformBytes) ||
        !detail::floatRange(view.other.uniformOffset, view.uniformBytes)) return MaterialStatus::UniformRange;
    if (view.character.uniformOffset == view.other.uniformOffset) return MaterialStatus::AliasedParameters;
    const auto source = reinterpret_cast<std::uintptr_t>(nativeUniform.data());
    const auto target = reinterpret_cast<std::uintptr_t>(destination.data());
    const auto distance = source > target ? source - target : target - source;
    if (distance < view.uniformBytes) return MaterialStatus::OverlappingBuffers;
    std::memcpy(destination.data(), nativeUniform.data(), view.uniformBytes);
    std::memcpy(destination.data() + view.character.uniformOffset, &saturation.character, sizeof(float));
    std::memcpy(destination.data() + view.other.uniformOffset, &saturation.other, sizeof(float));
    return MaterialStatus::Ready;
}

} // namespace self_recall::palette
