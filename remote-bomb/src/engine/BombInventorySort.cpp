// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativeInventoryReader.hpp"

namespace remote_bomb::inventory {
namespace eng = totk::engine;
namespace {
constexpr auto kUpgrade = REMOTE_BOMB_UPGRADED ? Upgrade::Plus : Upgrade::Base;
void logMenuStock(std::uintptr_t base) {
    for (const auto shape : {Shape::Sphere, Shape::Cube}) {
        const auto stock = probe(base, shape, kUpgrade, true);
        Logging.Log("[remote-bomb] INVENTORY_MENU_STOCK form=%u status=%u slot=%d quantity=%d failed=%u stage=%u",
                    shape == Shape::Sphere ? 0u : 1u, unsigned(stock.status), stock.slot, stock.quantity,
                    stock.failed, stock.stage);
    }
}
}
void prioritizePouch(std::uintptr_t base, std::uintptr_t pouch, unsigned category, std::uintptr_t list) {
    if (!base || category != kMaterialCategory || !eng::isPlausibleAddress(pouch))
        return;
    static unsigned reports{};
    Reader reader(base, pouch);
    if (!reader.ready()) {
        if (report(reports))
            Logging.Log("[remote-bomb] INVENTORY_SORT list=materials result=-1 reason=reader count=%u",
                        reader.count);
        return;
    }
    ListReport out;
    auto stage = NameStage::None;
    const int result = prioritizeList(reinterpret_cast<SortList *>(list),
        [&](unsigned i, unsigned &rank) { return (stage = reader.classify(i, rank, false)) == NameStage::None; },
        validNode, &out);
    if (!report(reports))
        return;
    Logging.Log("[remote-bomb] INVENTORY_SORT list=materials result=%d count=%d refusal=%u step=%d "
                "detail=%llx index=%u stage=%u sphere=%d@%u cube=%d@%u",
                result, out.count, unsigned(out.refusal), out.step,
                static_cast<unsigned long long>(out.detail), out.index, unsigned(stage),
                out.position[0], out.slot[0], out.position[1], out.slot[1]);
    if (reports <= 8)
        logMenuStock(base);
}
void prioritizeShortcut(std::uintptr_t base, std::uintptr_t pouch) {
    if (!base || !eng::isPlausibleAddress(pouch))
        return;
    static unsigned reports{};
    const auto refuse = [&](const char *reason, int count, int at, unsigned stage = 0) {
        if (report(reports))
            Logging.Log("[remote-bomb] INVENTORY_SORT list=shortcut result=-1 reason=%s count=%d index=%d stage=%u",
                        reason, count, at, stage);
    };
    Reader reader(base, pouch);
    const int count = eng::readMemory<int>(pouch + 121848);
    const auto array = eng::readMemory<std::uintptr_t>(pouch + 121856);
    if (!reader.ready())
        return refuse("reader", count, -1);
    if (count < 0 || count > int(kMaximumItems) || !eng::isPlausibleAddress(array))
        return refuse("array", count, -1);
    struct Item { std::uintptr_t pointer; unsigned rank; };
    std::array<Item, kMaximumItems> items{};
    std::array<int, 2> position{-1, -1};
    for (int i = 0; i < count; ++i) {
        const auto item = eng::readMemory<std::uintptr_t>(array + sizeof(std::uintptr_t) * i);
        if (!eng::isPlausibleAddress(item))
            return refuse("item", count, i);
        unsigned rank = 2;
        if (eng::readMemory<unsigned>(item) == kMaterialCategory) {
            const auto stage = reader.classify(eng::readMemory<unsigned>(item + 4), rank, true);
            if (stage != NameStage::None)
                return refuse("name", count, i, unsigned(stage));
        }
        if (rank < 2 && position[rank] < 0)
            position[rank] = i;
        items[i] = {item, rank};
    }
    const bool changed = prioritize(items.data(), unsigned(count), [](Item item) { return item.rank; });
    if (changed)
        for (int i = 0; i < count; ++i)
            eng::writeMemory(array + sizeof(std::uintptr_t) * i, items[i].pointer);
    if (report(reports))
        Logging.Log("[remote-bomb] INVENTORY_SORT list=shortcut result=%d count=%d sphere=%d cube=%d",
                    int(changed), count, position[0], position[1]);
}
}
