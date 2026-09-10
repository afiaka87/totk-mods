#include "RecallActorReference.hpp"

namespace self_recall::model {
namespace {

constexpr std::uintptr_t kActorLinkGetReference = 0x00753530;
constexpr std::uintptr_t kBaseProcReferenceSetProc = 0x0086A288;

detail::NativeActorReferenceResult resolve(std::uintptr_t mainBase, const void* link) {
    if (!mainBase || !link) return {};
    using Resolve = detail::NativeActorReferenceResult (*)(const void*);
    return reinterpret_cast<Resolve>(mainBase + kActorLinkGetReference)(link);
}

}  // namespace

ScopedActorReference::ScopedActorReference(std::uintptr_t mainBase, const void* actorLink)
    : mainBase_(mainBase), reference_(resolve(mainBase, actorLink)) {}

ScopedActorReference::~ScopedActorReference() {
    if (!mainBase_ || !reference_.actor) return;
    using Clear = void (*)(detail::NativeActorReferenceResult*, const void*);
    reinterpret_cast<Clear>(mainBase_ + kBaseProcReferenceSetProc)(&reference_, nullptr);
}

}  // namespace self_recall::model
