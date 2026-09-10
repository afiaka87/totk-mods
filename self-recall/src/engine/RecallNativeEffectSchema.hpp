#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace self_recall::equipment_effects::detail {
inline constexpr unsigned kProperties = 128, kEnums = 512, kNames = 32768;
template<class T> T read(const void* p, std::size_t offset) {
    T v; std::memcpy(&v, static_cast<const std::byte*>(p) + offset, sizeof(v)); return v;
}
template<class T> void write(void* p, std::size_t offset, T v) {
    std::memcpy(static_cast<std::byte*>(p) + offset, &v, sizeof(v));
}
struct EnumValue { const char* name; std::int32_t value; std::uint32_t pad; };
struct NativeEffectSchema {
    NativeEffectSchema() = default;
    NativeEffectSchema(const NativeEffectSchema&) = delete;
    NativeEffectSchema& operator=(const NativeEffectSchema&) = delete;
    alignas(8) std::byte table[40]{};
    alignas(8) std::byte definitions[kProperties][112]{};
    const void* definitionPointers[kProperties]{};
    std::uint32_t values[kProperties]{};
    EnumValue enums[kEnums]{};
    char names[kNames]{};
    unsigned nameUsed = 0, enumUsed = 0, propertyCount = 0;
    const char* userName = nullptr;
};
inline const char* copyName(NativeEffectSchema& a, const char* source) {
    if (!source) return nullptr;
    unsigned length = 0;
    while (length < 1024 && source[length]) ++length;
    if (length == 1024 || a.nameUsed + length + 1 > kNames) return nullptr;
    auto* out = a.names + a.nameUsed;
    std::memcpy(out, source, length + 1);
    a.nameUsed += length + 1;
    return out;
}
inline bool copySchema(NativeEffectSchema& a, const void* source) {
    const auto* user = read<const void*>(source, 0x58);
    if (!user) return false;
    a.propertyCount = read<std::uint16_t>(user, 0x44);
    a.nameUsed = a.enumUsed = 0;
    if (a.propertyCount > kProperties) return false;
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
            if (count < 0 || capacity < count || unsigned(count) + a.enumUsed > kEnums || (count && !values)) return false;
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

} // namespace self_recall::equipment_effects::detail
