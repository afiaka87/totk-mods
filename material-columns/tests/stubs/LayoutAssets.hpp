#pragma once
#include <cstdint>
namespace material_columns::assets {
alignas(16) inline std::uint8_t parent[20]{};
inline constexpr std::uint32_t parentCrc=0xf35961a4;
alignas(16) inline std::uint8_t columns[20]{};
inline constexpr std::uint32_t columnsCrc=0xf35961a4;
alignas(16) inline std::uint8_t sort[20]{};
inline constexpr std::uint32_t sortCrc=0xf35961a4;
alignas(16) inline std::uint8_t slot[20]{};
inline constexpr std::uint32_t slotCrc=0xf35961a4;
}
