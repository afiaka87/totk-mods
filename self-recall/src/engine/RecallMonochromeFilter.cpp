#include "RecallMonochromeFilter.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <lib.hpp>
#include "RecallMonochrome.hpp"
#include "RecallNativeSceneMaterial.hpp"
#include "RecallOffsets121.hpp"
#include "RecallPoseRender.hpp"

namespace self_recall::monochrome {
namespace {
using namespace offsets121::palette;
std::uintptr_t g_mainBase = 0;
std::atomic<std::uintptr_t> g_filter{0};
std::atomic<std::uint64_t> g_epoch{0}, g_failure{0}, g_draws{0}, g_materials{0};
std::atomic<std::uint32_t> g_generation{0};

enum class Failure : unsigned { Filter = 1, Shader, ZeroTexture, Model, Material,
    Parameter, Attribute, Upload };
void fail(Failure reason, unsigned detail = 0) {
    std::uint64_t empty = 0;
    if (g_failure.compare_exchange_strong(empty, (std::uint64_t(reason) << 32) | detail))
        Logging.Log("[self-recall] MONOCHROME_FAILURE reason=%u detail=%u epoch=%llu",
            unsigned(reason), detail, static_cast<unsigned long long>(g_epoch.load()));
}
template<class T> T read(const void* object, std::size_t offset) {
    return palette::detail::read<T>(object, offset);
}
template<class F> F native(std::uintptr_t offset) {
    return reinterpret_cast<F>(g_mainBase + offset);
}
struct Frame { std::uint64_t epoch; std::uint32_t generation; };
Frame currentFrame() {
    const auto epoch = g_epoch.load(std::memory_order_acquire);
    const auto generation = g_generation.load(std::memory_order_relaxed);
    if (!epoch || !generation || g_epoch.load(std::memory_order_acquire) != epoch ||
        pose_render::latchedGeneration(epoch) != generation) return {};
    return {epoch, generation};
}

HOOK_DEFINE_TRAMPOLINE(FilterDrawHook) {
    static u64 Callback(void* filter, void* drawContext, unsigned view, void* mask,
                        int maskCount, void* context, void* buffers) {
        const auto frame = currentFrame();
        if (!frame.epoch || view != 0 || reinterpret_cast<std::uintptr_t>(filter) !=
                g_filter.load(std::memory_order_acquire))
            return Orig(filter, drawContext, view, mask, maskCount, context, buffers);
        if (!filter || !drawContext || !context || !buffers) {
            fail(Failure::Filter); return 0;
        }
        const auto* program = read<const void*>(filter, 8);
        const auto* variations = program ? read<const void*>(program, 8) : nullptr;
        const auto* selected = variations ? read<const void*>(variations, 0x20) : nullptr;
        if (!variations || !read<unsigned>(variations, 0x18) || !selected ||
            read<std::uint16_t>(selected, 0x22) != 1) {
            fail(Failure::Shader); return 0;
        }
        const auto* slot = read<const void*>(reinterpret_cast<void*>(g_mainBase), kPrimitiveTexturesSlot);
        const auto* primitives = slot ? read<const void*>(slot, 0) : nullptr;
        auto* zero = primitives ? read<void*>(primitives, 8 + 10 * 8) : nullptr;
        if (!zero) { fail(Failure::ZeroTexture); return 0; }
        alignas(16) std::array<std::byte, pure::kMonochromeFilterBytes> input;
        const auto built = pure::buildMonochromeFilter(
            {static_cast<const std::byte*>(filter), input.size()}, input);
        if (built != pure::MonochromeFilterStatus::Ready) {
            fail(Failure::Filter, unsigned(built)); return 0;
        }
        const auto result = Orig(input.data(), drawContext, view, zero, 0, context, buffers);
        const auto count = g_draws.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count == 1 || count % 1800 == 0)
            Logging.Log("[self-recall] MONOCHROME_DRAW count=%llu epoch=%llu generation=%u variant=1 mask=zero2d chara_milli=375 world_milli=250 protected=%llu",
                static_cast<unsigned long long>(count), static_cast<unsigned long long>(frame.epoch),
                frame.generation, static_cast<unsigned long long>(g_materials.load()));
        return result;
    }
};

bool protectMaterial(void* model, unsigned materialIndex, unsigned buffer) {
    auto* materials = read<std::byte*>(model, 0x180);
    if (!materials) { fail(Failure::Material, materialIndex); return false; }
    auto* material = materials + materialIndex * 0x80;
    const auto* resource = read<const void*>(material, 0);
    const auto* assignment = resource ? read<const void*>(resource, 0x10) : nullptr;
    const auto* shader = assignment ? read<const void*>(assignment, 0) : nullptr;
    const auto* parameters = shader ? read<const std::byte*>(shader, 0x20) : nullptr;
    const auto* mapping = resource ? read<const void*>(resource, 0x60) : nullptr;
    auto* source = read<const std::byte*>(material, 0x48);
    const auto* buffers = read<const std::byte*>(material, 0x40);
    const auto bufferCount = read<std::uint8_t>(material, 0xA);
    if (!shader || !parameters || !mapping || !source || !buffers || !bufferCount ||
        bufferCount > 3 || buffer >= bufferCount || !(read<std::uint16_t>(material, 8) & 1u)) {
        fail(Failure::Material, materialIndex); return false;
    }
    const char* name = "p_object_attribute";
    const int index = native<int (*)(void*, int, const char**)>(kFindMaterialParameter)(model, materialIndex, &name);
    if (index < 0 || index >= read<std::uint16_t>(shader, 0x4A)) {
        fail(Failure::Parameter, materialIndex << 16); return false;
    }
    const auto* parameter = parameters + index * 0x18;
    const auto sourceOffset = read<std::uint16_t>(parameter, 0x10);
    const auto gpuOffset = read<std::int32_t>(mapping, index * 4);
    const auto gpuBytes = read<std::uint64_t>(material, 0x60);
    if (read<std::uintptr_t>(parameter, 0) || read<std::uint8_t>(parameter, 0x12) != 12 ||
        !palette::detail::floatRange(sourceOffset, read<std::uint16_t>(shader, 0x4C)) ||
        !gpuBytes || gpuBytes > UINT16_MAX || gpuBytes != read<std::uint16_t>(resource, 0xAA) ||
        gpuOffset < 0 || !palette::detail::floatRange(gpuOffset, static_cast<unsigned>(gpuBytes))) {
        fail(Failure::Parameter, (materialIndex << 16) | unsigned(index)); return false;
    }
    const float original = read<float>(source, sourceOffset);
    float excluded = original;
    const auto encoded = pure::enableMonochromeExclusion(original, excluded);
    if (encoded != pure::ObjectAttributeStatus::Ready) {
        fail(Failure::Attribute, unsigned(encoded)); return false;
    }
    const auto* nativeBuffer = buffers + buffer * 0x48;
    if (!read<std::uintptr_t>(nativeBuffer, 8)) {
        Logging.Log("[self-recall] MONOCHROME_UPLOAD buffer_missing model=%p material=%u parameter=%d buffer=%u",
            model, materialIndex, index, buffer);
        fail(Failure::Upload, (materialIndex << 16) | unsigned(index)); return false;
    }
    const auto set = native<u64 (*)(void*, int, int, float)>(kWriteSaturation);
    pure::uploadMonochromeAttribute(original, excluded,
        [&](float value) { set(model, materialIndex, index, value); },
        [&] { native<void (*)(void*, unsigned)>(kCalculateMaterial)(material, buffer); });
    const auto* mapped = native<const std::byte* (*)(const void*)>(kMapUniform)(nativeBuffer);
    if (!mapped || read<float>(mapped, gpuOffset) != excluded || read<float>(source, sourceOffset) != original) {
        Logging.Log("[self-recall] MONOCHROME_UPLOAD mismatch model=%p material=%u parameter=%d buffer=%u mapped=%u source=%08x expected_source=%08x gpu=%08x expected_gpu=%08x dirty=%u pending=%u",
            model, materialIndex, index, buffer, unsigned(mapped != nullptr),
            std::bit_cast<unsigned>(read<float>(source, sourceOffset)), std::bit_cast<unsigned>(original),
            mapped ? std::bit_cast<unsigned>(read<float>(mapped, gpuOffset)) : 0,
            std::bit_cast<unsigned>(excluded), unsigned(read<std::uint8_t>(material, 0x2C)),
            unsigned(read<std::uint8_t>(material, 0x2D)));
        fail(Failure::Upload, (materialIndex << 16) | unsigned(index)); return false;
    }
    const auto count = g_materials.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count == 1 || count % 1800 == 0)
        Logging.Log("[self-recall] MONOCHROME_LINK count=%llu material=%u parameter=%d buffer=%u source_offset=%u gpu_offset=%d original=%08x excluded=%08x restored=1",
            static_cast<unsigned long long>(count), materialIndex, index, buffer,
            unsigned(sourceOffset), gpuOffset, std::bit_cast<unsigned>(original), std::bit_cast<unsigned>(excluded));
    return true;
}
} // namespace

void initializeForScene(const void* extension) {
    g_filter.store(extension ? read<std::uintptr_t>(extension, 0xAB8) : 0, std::memory_order_release);
}
void beginFrame(std::uint64_t epoch, std::uint32_t generation) {
    const auto previous = g_generation.load(std::memory_order_relaxed);
    g_epoch.store(0, std::memory_order_release);
    g_generation.store(generation, std::memory_order_relaxed);
    g_epoch.store(epoch, std::memory_order_release);
    if (previous != generation && previous)
        Logging.Log("[self-recall] MONOCHROME_SUMMARY generation=%u draws=%llu protected=%llu",
            previous, static_cast<unsigned long long>(g_draws.load()),
            static_cast<unsigned long long>(g_materials.load()));
}
void protectHistoricalModel(void* model) {
    const auto frame = currentFrame();
    if (!frame.epoch) return;
    if (pose_render::paletteModel(model, frame.epoch, frame.generation) != pose_render::PaletteModel::Ready)
        return;
    const auto count = read<std::uint16_t>(model, 0x16A);
    const auto buffer = read<std::uint8_t>(model, 0x15) & 3u;
    for (unsigned i = 0; i < count; ++i)
        if (!protectMaterial(model, i, buffer)) return;
}
std::uint64_t takeFailure() { return g_failure.exchange(0, std::memory_order_acq_rel); }
void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    FilterDrawHook::InstallAtOffset(kDrawMonochromeFilter);
}
} // namespace self_recall::monochrome
