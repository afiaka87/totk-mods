#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace self_recall::model {

class ResourceLease {
public:
    ResourceLease() = default;
    ResourceLease(const ResourceLease&) = delete;
    ResourceLease& operator=(const ResourceLease&) = delete;
    bool retain(std::uintptr_t mainBase, const void* sourceBinder);
    void release();
    const void* resource() const { return resource_; }
    const void* binder() const { return binder_; }
    bool held() const { return mainBase_ != 0; }
private:
    alignas(8) std::byte binder_[40]{};
    std::uintptr_t mainBase_ = 0;
    const void* resource_ = nullptr;
};

} // namespace self_recall::model
