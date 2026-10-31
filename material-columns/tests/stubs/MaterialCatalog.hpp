// Synthetic catalog for mocked integration; real catalog has separate owned-data tests.
#pragma once
#include <cstdint>
namespace material_columns::catalog {
struct Entry { std::uint64_t item, group; std::uint32_t rank; };
struct Group { std::uint64_t id; std::uint32_t rank; const char* label; };
inline constexpr auto fruit = identity("group:fruit");
inline constexpr auto eyes = identity("group:eyeballs");
inline constexpr Entry items[]{{10, fruit, 10}, {11, fruit, 11}, {12, fruit, 12}, {20, eyes, 20}};
inline constexpr Group groups[]{{fruit, 10, "Fruit"}, {eyes, 20, "Eyes"},
    {identity("group:zonai-devices"), 30, "Zonai"}, {identity("group:other-materials"), 40, "Other"},
    {identity("group:mushrooms"), 12, "Mushrooms"}, {identity("group:special-materials"), 29, "Special materials"},
    {identity("group:wings"), 21, "Wings"}, {identity("group:star-fragments"), 22, "Star fragments"},
    {identity("group:zonaite"), 23, "Zonaite"}};
}
