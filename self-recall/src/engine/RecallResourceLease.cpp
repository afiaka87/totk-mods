#include "RecallResourceLease.hpp"

namespace self_recall::model {
namespace {
template<class T> T read(const void* p, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof(value));
    return value;
}

struct BinderReferenceArgument {
    std::uintptr_t vtable;
    std::uint8_t kind = 3, synchronous = 1, required = 1;
    std::byte padding[5]{};
    const char* requester = "SelfRecallEquipment";
    const void* source;
    std::uintptr_t reserved = 0;
};
static_assert(sizeof(BinderReferenceArgument) == 40);
static_assert(offsetof(BinderReferenceArgument, source) == 0x18);
} // namespace

bool ResourceLease::retain(std::uintptr_t mainBase, const void* sourceBinder) {
    if (held() || !mainBase || !sourceBinder) return false;
    using Get = const void* (*)(const void*);
    const auto get = reinterpret_cast<Get>(mainBase + 0x00B51804);
    const auto* sourceResource = get(sourceBinder);
    if (!sourceResource || !read<const void*>(sourceBinder, 8)) return false;
    using Construct = void (*)(void*);
    reinterpret_cast<Construct>(mainBase + 0x00BC7A48)(binder_);
    mainBase_ = mainBase;
    const BinderReferenceArgument argument{mainBase + 0x045C80F0, 3, 1, 1, {},
                                          "SelfRecallEquipment", sourceBinder, 0};
    const char* empty = "";
    using Retain = const void* (*)(void*, const char* const*, const void*, int*);
    resource_ = reinterpret_cast<Retain>(mainBase + 0x0076F8F0)(binder_, &empty, &argument, nullptr);
    if (resource_ != sourceResource ||
        read<const void*>(binder_, 8) != read<const void*>(sourceBinder, 8)) {
        release();
        return false;
    }
    return true;
}

void ResourceLease::release() {
    if (!mainBase_) return;
    using Destroy = void (*)(void*);
    reinterpret_cast<Destroy>(mainBase_ + 0x00770B8C)(binder_);
    resource_ = nullptr;
    mainBase_ = 0;
}

} // namespace self_recall::model
