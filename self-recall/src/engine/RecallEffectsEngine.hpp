#pragma once

#include <cstddef>
#include <cstdint>
#include "RecallBase.hpp"

namespace self_recall::equipment {
inline constexpr std::size_t kArchiveHeapBytes = pure::kArchiveHeapBytes;
void installHeap(std::uintptr_t mainBase);
void* archiveHeap();
std::size_t contiguousArchiveBytes();
bool archiveOwns(const void* address);
void* allocateArchive(std::size_t bytes);
void freeArchive(void* address);
}

#include "RecallModelEngine.hpp"

namespace self_recall::equipment {
void initializeAppearance(std::uintptr_t mainBase);
bool captureAppearance(detail::Asset& asset, unsigned assetIndex, std::span<const model::View> source);
bool restoreAppearance(unsigned token, std::span<detail::Asset> assets);
void releaseAppearance(unsigned token);
void beginRecord(std::uint32_t historyGeneration);
bool bindAppearanceFrame(pure::PoseFrameKey key, std::span<const unsigned> tokens);
unsigned appearanceToken(pure::PoseFrameKey key, unsigned model);
bool refuseAppearance(const char* reason, std::uint64_t detail, std::uint64_t extra);
void collectAppearance(const pure::PoseHistory& history);
bool captureBodyAppearance(const pure::RecordedPoseFrame& frame, std::span<unsigned> tokens);
class BodyAppearanceScope {
public:
    BodyAppearanceScope(pure::PoseFrameKey key, unsigned modelIndex, const model::Identity& live);
    ~BodyAppearanceScope();
    BodyAppearanceScope(const BodyAppearanceScope&) = delete;
    BodyAppearanceScope& operator=(const BodyAppearanceScope&) = delete;
    bool ready() const { return buffer_ >= 0; }
private:
    int buffer_ = -1;
};
}

#include "RecallAppearance.hpp"

namespace self_recall::equipment_effects {
struct Values {
    std::uint32_t count = 0;
    float scale[3]{1, 1, 1};
    std::uint32_t properties[128]{};
};
void captureValues(unsigned asset, Values& out);
bool applyValues(unsigned asset, const Values& values);
void install(std::uintptr_t mainBase);
bool retain(unsigned asset, const void* actor, const void* copiedRoot);
void retire(unsigned asset);
void record(unsigned asset, pure::PoseFrameHeader& header);
void selectFrame(const pure::RecordedPoseFrame* frame);
void publishLive(std::span<const void* const> actors);
bool copyMatrix(const void* descriptor, float out[12]);
}

#include <cstring>
#include <span>

namespace self_recall::equipment_effects::detail {
inline constexpr unsigned kProperties = 128, kEnums = 512, kNames = 32768;
template<class T> T read(const void* p, std::size_t offset) {
    T v; std::memcpy(&v, static_cast<const std::byte*>(p) + offset, sizeof(v)); return v;
}
template<class T> void write(void* p, std::size_t offset, T v) {
    std::memcpy(static_cast<std::byte*>(p) + offset, &v, sizeof(v));
}
struct EnumValue { const char* name; std::int32_t value; std::uint32_t pad; };
template<class Schema> inline const char* copyName(Schema& a, const char* source) {
    if (!source) return nullptr;
    unsigned length = 0;
    while (length < 1024 && source[length]) ++length;
    if (length == 1024 || a.nameUsed + length + 1 > a.nameCapacity) return nullptr;
    auto* out = a.names + a.nameUsed;
    std::memcpy(out, source, length + 1);
    a.nameUsed += length + 1;
    return out;
}
template<class Schema> inline bool copySchema(Schema& a, const void* source) {
    const auto* user = read<const void*>(source, 0x58);
    if (!user) return false;
    a.propertyCount = read<std::uint16_t>(user, 0x44);
    a.nameUsed = a.enumUsed = 0;
    if (a.propertyCount > a.propertyCapacity) return false;
    a.userName = copyName(a, read<const char*>(user, 0x10));
    if (!a.userName || !*a.userName) return false;
    const auto* definitions = read<const void* const*>(user, 0x48);
    if (a.propertyCount && !definitions) return false;
    for (unsigned i = 0; i < a.propertyCount; ++i) {
        const auto* from = definitions[i];
        if (!from) return false;
        const auto type = read<std::uint32_t>(from, 0x58);
        if (type > 5) return false;
        auto* to = a.definitions[i];
        std::memcpy(to, from, type == 0 ? 112 : type <= 2 ? 104 : 96);
        const auto* name = read<const char*>(from, 8);
        unsigned length = 0;
        if (!name) return false;
        while (length < 64 && name[length]) ++length;
        if (length == 64) return false;
        std::memcpy(to + 20, name, length + 1);
        write<const void*>(to, 8, to + 20);
        write<std::uint32_t>(to, 16, 64);
        if (type == 0) {
            const auto count = read<std::int32_t>(from, 96);
            const auto capacity = read<std::int32_t>(from, 100);
            const auto* values = read<const EnumValue*>(from, 104);
            if (count < 0 || capacity < count || unsigned(count) + a.enumUsed > a.enumCapacity || (count && !values)) return false;
            auto* owned = a.enums + a.enumUsed;
            for (int j = 0; j < count; ++j) {
                owned[j] = values[j];
                owned[j].name = copyName(a, values[j].name);
                if (!owned[j].name) return false;
            }
            a.enumUsed += count;
            write(to, 96, count);
            write(to, 100, count);
            write(to, 104, owned);
        }
        a.definitionPointers[i] = to;
    }
    std::memset(a.table, 0, sizeof(a.table));
    write(a.table, 24, a.propertyCount);
    write<const void*>(a.table, 32, a.definitionPointers);
    return true;
}

struct EffectSchemaSize {
    unsigned properties = 0, enums = 0, names = 0;
    unsigned enumOffset() const { return (properties * 124u + 7u) & ~7u; }
    unsigned bytes() const { return enumOffset() + enums * sizeof(EnumValue) + names; }
    explicit operator bool() const { return names != 0; }
};

inline EffectSchemaSize measureSchema(const void* source) {
    const auto* user = source ? read<const void*>(source, 0x58) : nullptr;
    if (!user) return {};
    EffectSchemaSize size;
    size.properties = read<std::uint16_t>(user, 0x44);
    if (size.properties > kProperties) return {};
    const auto addName = [&](const char* name) {
        if (!name) return false;
        unsigned length = 0;
        while (length < 1024 && name[length]) ++length;
        if (length == 1024 || size.names + length + 1 > kNames) return false;
        size.names += length + 1;
        return true;
    };
    const auto* name = read<const char*>(user, 0x10);
    if (!name || !*name || !addName(name)) return {};
    const auto* definitions = read<const void* const*>(user, 0x48);
    if (size.properties && !definitions) return {};
    for (unsigned i = 0; i < size.properties; ++i) {
        const auto* from = definitions[i];
        if (!from) return {};
        const auto type = read<std::uint32_t>(from, 0x58);
        if (type > 5) return {};
        if (type != 0) continue;
        const auto count = read<std::int32_t>(from, 96);
        const auto capacity = read<std::int32_t>(from, 100);
        const auto* values = read<const EnumValue*>(from, 104);
        if (count < 0 || capacity < count || unsigned(count) > kEnums - size.enums || (count && !values)) return {};
        size.enums += count;
        for (int j = 0; j < count; ++j) if (!addName(values[j].name)) return {};
    }
    return size;
}

struct PackedEffectSchema {
    alignas(8) std::byte table[40]{};
    std::byte (*definitions)[112] = nullptr;
    const void** definitionPointers = nullptr;
    std::uint32_t* values = nullptr;
    EnumValue* enums = nullptr;
    char* names = nullptr;
    unsigned nameUsed = 0, enumUsed = 0, propertyCount = 0;
    const char* userName = nullptr;
    unsigned propertyCapacity = 0, enumCapacity = 0, nameCapacity = 0;
    std::byte* storage = nullptr;

    bool bind(std::span<std::byte> bytes, EffectSchemaSize size) {
        if (!size || size.properties > kProperties || size.enums > kEnums || size.names > kNames ||
            bytes.size() < size.bytes() || reinterpret_cast<std::uintptr_t>(bytes.data()) % 8) return false;
        std::memset(bytes.data(), 0, size.bytes());
        storage = bytes.data();
        definitions = reinterpret_cast<std::byte (*)[112]>(storage);
        definitionPointers = reinterpret_cast<const void**>(storage + size.properties * 112u);
        values = reinterpret_cast<std::uint32_t*>(storage + size.properties * 120u);
        enums = reinterpret_cast<EnumValue*>(storage + size.enumOffset());
        names = reinterpret_cast<char*>(enums + size.enums);
        propertyCapacity = size.properties; enumCapacity = size.enums; nameCapacity = size.names;
        return true;
    }
};

}

namespace self_recall::monochrome {
void install(std::uintptr_t mainBase);
void initializeForScene(const void* extension);
void beginFrame(std::uint64_t epoch, std::uint32_t generation);
void protectHistoricalModel(void* model);
std::uint64_t takeFailure();
}

#include "RecallRender.hpp"

namespace self_recall::wrist_effects {

void install(std::uintptr_t mainBase);
void begin(std::uint32_t historyGeneration, std::span<const pure::CompactEffectHandle> handles);
void end();

}
