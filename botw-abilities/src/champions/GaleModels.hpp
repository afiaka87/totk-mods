// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "GalePoseMatrices.hpp"
#include "totk/engine/ActorReference121.hpp"
#include "totk/engine/NativeRead.hpp"
#include <optional>
#include <string_view>

namespace botw_abilities::champions::pose::models {
using totk::engine::native_read::admit;
using totk::engine::native_read::read;
using totk::engine::native_read::at;
inline const void* registry(const void* actor) { return read<const void*>(actor, 0x228); }
template<class Fn> Fn native(std::uintptr_t base, std::uintptr_t offset) { return reinterpret_cast<Fn>(base + offset); }

using ActorReference = totk::engine::ActorReference121;

struct Identity {
    std::uintptr_t unit = 0, skeleton = 0, resource = 0;
    std::uint16_t boneCount = 0, materialCount = 0;
};
struct Layout {
    std::size_t embeddedModel = 0x138, skeleton = 0x170, shapeCount = 0x168, shapeArray = 0x178;
};
inline constexpr Layout kLayout;
inline constexpr unsigned kModelBoneLimit = 256;

struct View {
    Identity identity;
    const PoseMatrix* matrices = nullptr;
    std::array<float, 3> origin{};
    bool relative = false;
};

inline unsigned boneCountOf(const void* unit) {
    return read<std::uint16_t>(read<const void*>(read<const void*>(unit, 0x170), 0), 0x38);
}

inline bool describe(std::uintptr_t base, const void* unit, View& view) {
    if (!admit(unit, 0, 0x360) || read<std::uintptr_t>(unit, 0) != base + 0x45C0570) return false;
    const auto* skeleton = read<const void*>(unit, 0x170);
    const auto* resource = read<const void*>(skeleton, 0);
    const auto* matrices = read<const PoseMatrix*>(skeleton, 0x20);
    const auto count = read<std::uint16_t>(resource, 0x38);
    if (!admit(skeleton, 0, 0x48) || !count || count > kModelBoneLimit ||
        !admit(matrices, 0, count * sizeof(PoseMatrix))) return false;
    view = {{reinterpret_cast<std::uintptr_t>(unit), reinterpret_cast<std::uintptr_t>(skeleton),
             reinterpret_cast<std::uintptr_t>(resource), count, read<std::uint16_t>(unit, 0x16A)},
             matrices, read<std::array<float, 3>>(unit, 0x338), bool(read<std::uint8_t>(unit, 0x355) & 1)};
    for (float value : view.origin) if (!std::isfinite(value)) return false;
    return true;
}

inline std::string_view boneName(const View& view, unsigned bone) {
    if (bone >= view.identity.boneCount) return {};
    const auto* resource = reinterpret_cast<const void*>(view.identity.resource);
    const auto* dictionary = read<const void*>(resource, 8);
    const auto* text = read<const char*>(dictionary, 0x20 + bone * 0x10);
    const auto size = read<std::uint16_t>(text, 0);
    return size && size <= 255 && admit(text, 2, size) ? std::string_view(text + 2, size) : std::string_view{};
}
inline int parent(const View& view, unsigned bone) {
    if (bone >= view.identity.boneCount) return -1;
    const auto* metadata = read<const void*>(reinterpret_cast<const void*>(view.identity.resource), 0x10);
    return read<std::int16_t>(metadata, bone * 0x58 + 0x22);
}
inline bool worldMatrix(const View& view, unsigned bone, PoseMatrix& out) {
    if (bone >= view.identity.boneCount) return false;
    auto value = read<PoseMatrix>(view.matrices, bone * sizeof(PoseMatrix));
    value.at(3,0) = value.at(3,1) = value.at(3,2) = 0; value.at(3,3) = 1;
    if (!finiteAffine(value) || (view.relative && !rebasePoseOrigin(value, view.origin, {}))) return false;
    out = value;
    return true;
}

template<class Visit> bool visitModels(const void* actor, Visit visit) {
    const auto* component = read<const void*>(registry(actor), 0x10);
    const auto* root = read<const void*>(component, 0x28);
    if (!root) return true;
    const auto count = read<int>(root, 0x20);
    const auto* entries = read<const void* const*>(root, 0x28);
    if (count < 0 || count > 64 || (count && !admit(entries, 0, count * sizeof(void*)))) return false;
    for (int i = 0; i < count; ++i) {
        const auto* entry = read<const void*>(entries, i * sizeof(void*));
        const auto* unit = read<const void*>(entry, 0);
        if (!unit || !visit(unit)) return false;
    }
    return true;
}
inline bool containsModel(const void* actor, const void* unit) {
    bool found = false;
    const bool valid = visitModels(actor, [&](const void* candidate) { found |= candidate == unit; return true; });
    return valid && found;
}

template<class Visit> bool visitLinks(const void* player, Visit visit) {
    const auto* components = registry(player);
    const auto* equipment = read<const void*>(components, 0x230);
    if (equipment) {
        for (unsigned i = 0; i < 8; ++i) if (!visit(at(equipment, 0x20 + 0x18*i))) return false;
        for (unsigned i = 0; i < 12; ++i) if (!visit(at(equipment, 0xE0 + 0x18*i))) return false;
        for (unsigned i = 0; i < 8; ++i) if (!visit(at(equipment, 0x580 + 0x100*i))) return false;
        if (!visit(at(equipment, 0x3E90))) return false;
    }
    const auto* component = read<const void*>(components, 0x3A8);
    return !component || (visit(at(component, 0x6B8)) && visit(at(component, 0x6D0)));
}
inline const void* fuseLink(const void* actor) {
    const auto* equipment = read<const void*>(registry(actor), 0x208);
    return read<std::uint8_t>(equipment, 0x50C) ? at(equipment, 0xB0) : nullptr;
}

struct Owner {
    const void* actor = nullptr;
    std::optional<ActorReference> primary, fused;
};

inline bool findOwner(std::uintptr_t base, const void* player, const void* unit, Owner& out) {
    if (containsModel(player, unit)) { out.actor = player; return true; }
    bool found = false;
    visitLinks(player, [&](const void* link) {
        if (!link) return true;
        out.fused.reset();
        out.primary.emplace(base, link);
        const auto* actor = out.primary->get();
        if (!actor) return true;
        if (containsModel(actor, unit)) { out.actor = actor; found = true; return false; }
        const auto* fused = fuseLink(actor);
        if (fused) {
            out.fused.emplace(base, fused);
            if (const auto* child = out.fused->get(); child && containsModel(child, unit)) {
                out.actor = child; found = true; return false;
            }
        }
        return true;
    });
    return found;
}

inline int poseBone(const GalePose& asset, std::string_view name) {
    for (unsigned bone = 0; bone < asset.boneCount(); ++bone) if (asset.boneName(bone) == name) return int(bone);
    return -1;
}
inline bool findBone(std::uintptr_t base, const void* actor, std::string_view name, View& out, unsigned& index) {
    bool found = false;
    const auto valid = visitModels(actor, [&](const void* unit) {
        View candidate;
        if (!describe(base, unit, candidate)) return true;
        for (unsigned bone = 0; bone < candidate.identity.boneCount; ++bone)
            if (boneName(candidate, bone) == name) { out = candidate; index = bone; found = true; break; }
        return true;
    });
    return valid && found;
}
} // namespace botw_abilities::champions::pose::models
