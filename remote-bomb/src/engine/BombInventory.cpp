// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombInventory.hpp"
#include "BombWorld.hpp"
#include "NativeInventoryReader.hpp"
#include <lib.hpp>
#include <cstring>

namespace remote_bomb::inventory {
namespace eng = totk::engine;
namespace {
struct NameRef { const char *text; std::uint64_t pad[3]{}; };
using ReadCount = unsigned (*)(std::uintptr_t, int *, const void *, unsigned);
using ResolveCategory = unsigned char (*)(unsigned *, const char **);
using AddItem = std::uint64_t (*)(void *, const char **, void *, unsigned, int, unsigned char,
                                 int, unsigned char, unsigned, unsigned, unsigned, unsigned,
                                 unsigned, unsigned, unsigned *, unsigned char);
bool writeRoom(std::uintptr_t manager) {
    // Material acquisition writes discovery flags, counts and one array name (CE0EEC).
    for (const auto offset : {8, 200, 296, 1640}) {
        const auto store = manager + offset;
        const auto capacity = eng::readMemory<int>(store + 40);
        const auto buffer = eng::readMemory<std::uintptr_t>(store + 48);
        const auto used = eng::readMemory<unsigned>(store + 56) & 0xfffffu;
        const unsigned needed = offset == 1640 ? 2 : 8;
        if (!queueCanFit(capacity, used, needed, eng::isPlausibleAddress(buffer))) {
            static unsigned attempts{};
            if (report(attempts))
                Logging.Log("[remote-bomb] INVENTORY_QUEUE store=%u capacity=%d used=%u needed=%u",
                            offset, capacity, used, needed);
            return false;
        }
    }
    return true;
}
Stock scan(std::uintptr_t base, std::uintptr_t manager, std::uintptr_t pouch, const char *name,
           bool queued) {
    Reader reader(base, pouch);
    if (!reader.ready())
        return {Status::Unavailable, -1, 0, reader.count};
    auto stage = NameStage::None;
    auto found = locateStock(reader.count, name, [&](unsigned i, Record &out) {
        alignas(8) std::uint64_t record[2]{};
        stage = reader.name(i, out.name, record, queued);
        if (stage != NameStage::None)
            return false;
        return std::strcmp(out.name, name) ||
            (reinterpret_cast<ReadCount>(base + 0x92FF20)(manager, &out.quantity, record, 0x22344481) & 1);
    });
    found.stage = unsigned(stage);
    return found;
}
Status stock(std::uintptr_t base, std::uintptr_t manager, std::uintptr_t pouch, const char *name) {
    const auto found = scan(base, manager, pouch, name, true);
    if (found.status == Status::Unavailable)
        return deferred("stock-open", found.status, found.failed);
    if (found.status == Status::Deferred)
        return deferred("stock-read", found.status, found.failed | std::uintptr_t(found.stage) << 32);
    return found.status;
}
}
Stock probe(std::uintptr_t base, Shape shape, Upgrade upgrade, bool committed) {
    const auto manager = base ? managerAt(base + 0x462E3D8) : 0;
    const auto pouch = base ? managerAt(base + 0x4630020) : 0;
    if (!manager || !pouch)
        return {Status::Unavailable};
    return scan(base, manager, pouch, engine::actorName(shape, upgrade), !committed);
}
Status ensure(std::uintptr_t base, Shape shape, Upgrade upgrade, bool workerAdvanced) {
    if (!base || !workerAdvanced)
        return deferred("gameplay", Status::Deferred);
    const auto manager = managerAt(base + 0x462E3D8);
    const auto pouch = managerAt(base + 0x4630020);
    if (!manager || !pouch)
        return deferred("manager", Status::Unavailable);
    NameRef name{engine::actorName(shape, upgrade)};
    auto *ref = reinterpret_cast<const char **>(&name);
    unsigned category{};
    if (!(reinterpret_cast<ResolveCategory>(base + 0x1706D80)(&category, ref) & 1) ||
        category != kMaterialCategory) {
        static unsigned reports{};
        if (reports++ < 12)
            Logging.Log("[remote-bomb] INVENTORY_CATEGORY_REFUSED actor=%s category=%u", name.text, category);
        return Status::Rejected;
    }
    const auto existing = stock(base, manager, pouch, name.text);
    if (existing != Status::Missing)
        return existing;
    if (!writeRoom(manager))
        return Status::Deferred;
    const auto empty = eng::readMemory<void *>(base + 0x462E0F0);
    if (!eng::isPlausibleAddress(reinterpret_cast<std::uintptr_t>(empty)))
        return deferred("empty-reference", Status::Unavailable);
    const auto result = reinterpret_cast<AddItem>(base + 0xCE0EEC)(
        reinterpret_cast<void *>(pouch), ref, empty, category, 1, 1, 0, 0,
        0, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, nullptr, 1);
    Logging.Log("[remote-bomb] INVENTORY_GRANT actor=%s category=%u result=%llu", name.text,
                category, static_cast<unsigned long long>(result));
    return result ? Status::Granted : Status::Rejected;
}
}
