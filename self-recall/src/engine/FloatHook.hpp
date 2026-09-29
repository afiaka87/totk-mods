#pragma once

#include <cstddef>
#include <lib.hpp>

namespace self_recall {
// The float inline-hook assembly saves the integer registers above its 0x200-byte float block.
inline exl::hook::InlineCtx* integerRegisters(exl::hook::InlineFloatCtx* ctx) {
    static_assert(sizeof(exl::hook::InlineCtx) == 0xF8);
    return reinterpret_cast<exl::hook::InlineCtx*>(reinterpret_cast<std::byte*>(ctx) + 0x200);
}
}
