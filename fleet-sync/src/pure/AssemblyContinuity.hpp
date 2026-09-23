#pragma once
#include "pure/MatchedFormation.hpp"

namespace linked_stick::pure::matched {
struct Continuity {
    const char* reason = "same construction";
    bool safe = true, handleChanged = false, orderChanged = false, bodyChanged = false;
    unsigned before = 0, after = 0;
};

template <class Snapshot>
Continuity constructionContinuity(const Snapshot& original, const Snapshot& current) {
    Continuity out;
    auto refuse = [&](const char* reason) { out.reason = reason; out.safe = false; return out; };
    if (!original.valid() || !current.valid()) return refuse("invalid snapshot");
    if (original.receiver != current.receiver) return refuse("stick component changed");
    if (!original.memberCount || original.memberCount > 21 ||
        original.memberCount != current.memberCount) return refuse("member count changed");
    out.handleChanged = original.integrator != current.integrator;
    for (unsigned i = 0; i < original.memberCount; ++i) {
        out.before = i;
        const auto& a = original.members[i];
        if (!a.actor || !a.nameIdentity || !a.rigidBody) return refuse("invalid original member");
        for (unsigned k = 0; k < i; ++k)
            if (original.members[k].actor == a.actor) return refuse("duplicate original actor");
        unsigned j = 0;
        for (; j < current.memberCount; ++j)
            if (current.members[j].actor == a.actor) break;
        out.after = j;
        if (j == current.memberCount) return refuse("actor replaced or detached");
        const auto& b = current.members[j];
        if (a.nameIdentity != b.nameIdentity || a.shape.kind != b.shape.kind)
            return refuse("actor identity changed");
        if (!b.rigidBody) return refuse("missing current body");
        for (unsigned k = 0; k < current.memberCount; ++k)
            if (k != j && (current.members[k].actor == b.actor ||
                           current.members[k].rigidBody == b.rigidBody))
                return refuse("duplicate current actor or body");
        if (!isFinite(a.shape.position) || !isFinite(b.shape.position) ||
            !validRotation(a.shape.rotation) || !validRotation(b.shape.rotation) ||
            length(sub(a.shape.position, b.shape.position)) > 0.20f ||
            length(rotationError(a.shape.rotation, b.shape.rotation)) > 0.12f)
            return refuse("member geometry changed");
        out.orderChanged |= i != j;
        out.bodyChanged |= a.rigidBody != b.rigidBody;
    }
    return out;
}

// This hash suppresses repeated diagnostics; it never admits an assembly.
template <class Snapshot>
std::uint64_t bindingFingerprint(const Snapshot& snapshot) {
    std::uint64_t hash = 14695981039346656037ull;
    const auto mix = [&](std::uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
    mix(snapshot.integrator);
    mix(snapshot.memberCount);
    for (unsigned i = 0; i < snapshot.memberCount && i < 21; ++i) {
        mix(snapshot.members[i].actor);
        mix(snapshot.members[i].nameIdentity);
        mix(snapshot.members[i].rigidBody);
    }
    return hash;
}
}
