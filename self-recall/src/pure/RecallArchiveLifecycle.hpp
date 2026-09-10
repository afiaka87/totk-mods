#pragma once

namespace self_recall::pure {
enum class ArchiveLife : unsigned { Empty, Constructing, Ready, Retiring, Destroyed };

constexpr bool archiveSuppressesUnselectedShapes(ArchiveLife life) {
    return life == ArchiveLife::Ready || life == ArchiveLife::Retiring;
}
} // namespace self_recall::pure
