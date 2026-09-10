#pragma once

#include <cstdint>
#include <type_traits>

namespace self_recall::model {
namespace detail {

struct NativeActorReferenceResult {
    void* actor = nullptr;
    std::uint8_t counted = 0;
    std::uint8_t reserved[7]{};
    ~NativeActorReferenceResult() {}
};
static_assert(sizeof(NativeActorReferenceResult) == 16);
static_assert(!std::is_trivially_destructible_v<NativeActorReferenceResult>);

}  // namespace detail

class ScopedActorReference {
public:
    ScopedActorReference(std::uintptr_t mainBase, const void* actorLink);
    ~ScopedActorReference();
    ScopedActorReference(const ScopedActorReference&) = delete;
    ScopedActorReference& operator=(const ScopedActorReference&) = delete;
    ScopedActorReference(ScopedActorReference&&) = delete;
    ScopedActorReference& operator=(ScopedActorReference&&) = delete;

    void* get() const { return reference_.actor; }
    explicit operator bool() const { return get() != nullptr; }

private:
    std::uintptr_t mainBase_ = 0;
    detail::NativeActorReferenceResult reference_{};
};

}  // namespace self_recall::model
