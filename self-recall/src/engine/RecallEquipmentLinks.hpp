#pragma once

#include <cstddef>
#include <initializer_list>
#include <span>

namespace self_recall::model {

enum class EquipmentLinkKind { Dynamic, Static, Attachment, ExtraAttachment };
inline constexpr std::size_t kOwnedActorLimit = 1 + 8 + 12 + 8 + 8 + 1 + 2;

template<class ReadPointer, class ReadByte>
const void* fusedEquipmentLink(const void* components, ReadPointer&& readPointer,
                               ReadByte&& readByte) {
    const auto* equipment = components ? readPointer(components, 0x208) : nullptr;
    return equipment && readByte(equipment, 0x50C)
        ? static_cast<const std::byte*>(equipment) + 0xB0 : nullptr;
}

inline bool isEquipmentParent(const void* parent, const void* player,
                              std::span<const void* const> owned) {
    if (!parent) return false;
    if (parent == player) return true;
    for (const auto* actor : owned) if (parent == actor) return true;
    return false;
}

template<class Visit>
bool visitEquipmentLinks(const void* equipment, Visit&& visit) {
    if (!equipment) return true;
    const auto* bytes = static_cast<const std::byte*>(equipment);
    for (unsigned i = 0; i < 8; ++i)
        if (!visit(bytes + 0x20 + 0x18 * i, EquipmentLinkKind::Dynamic, i)) return false;
    for (unsigned i = 0; i < 12; ++i)
        if (!visit(bytes + 0xE0 + 0x18 * i, EquipmentLinkKind::Static, i)) return false;
    for (unsigned i = 0; i < 8; ++i)
        if (!visit(bytes + 0x580 + 0x100 * i, EquipmentLinkKind::Attachment, i)) return false;
    return visit(bytes + 0x3E90, EquipmentLinkKind::ExtraAttachment, 0);
}

template<class ReadPointer, class MatchesParent>
bool hasBoundEquipmentParent(const void* components, ReadPointer&& readPointer,
                             MatchesParent&& matchesParent) {
    if (!components) return false;
    for (const auto offset : {std::size_t{0x18}, std::size_t{0x420}}) {
        const auto* bind = readPointer(components, offset);
        if (bind && matchesParent(static_cast<const std::byte*>(bind) + 0x70)) return true;
    }
    return false;
}

} // namespace self_recall::model
