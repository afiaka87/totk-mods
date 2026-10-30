// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/pure/StateJournal.hpp"
#include <cassert>
#include <vector>
using namespace material_columns;
using namespace material_columns::storage;
struct Files {
    std::vector<std::uint8_t> slots[2];
    bool failRead{}, failWrite{}, tearWrite{}, corruptWrite{};
    int writes{};
    FileRead read(int slot, std::uint8_t* out, std::size_t capacity, std::size_t& size) {
        if (failRead) return FileRead::error;
        if (slots[slot].empty()) return FileRead::missing;
        assert(slots[slot].size() <= capacity);
        size = slots[slot].size();
        std::memcpy(out, slots[slot].data(), size);
        return FileRead::present;
    }
    bool write(int slot, const std::uint8_t* bytes, std::size_t size) {
        ++writes;
        if (failWrite) return false;
        slots[slot].assign(bytes, bytes + (tearWrite ? size / 2 : size));
        if (corruptWrite) slots[slot][20] ^= 1;
        return true;
    }
};
int main() {
    const ProfileKey key{1, 2, 3};
    Preferences<4> value;
    value.columns = true;
    value.count = 1;
    value.items[0] = {10, 101, 1};
    Files files;
    StateJournal<4> journal;
    assert(!journal.save(value, files) && files.writes == 0);
    assert(journal.load(key, value, files) == LoadResult::missing);
    assert(journal.save(value, files));
    const auto first = files.slots[0];
    value.items[0].item = 102;
    files.tearWrite = true;
    assert(!journal.save(value, files));
    assert(files.slots[0] == first);
    StateJournal<4> restarted;
    Preferences<4> restored;
    assert(restarted.load(key, restored, files) == LoadResult::loaded && restored.items[0].item == 101);
    files.tearWrite = false;
    assert(restarted.save(value, files));
    StateJournal<4> again;
    assert(again.load(key, restored, files) == LoadResult::loaded && restored.items[0].item == 102);
    files.corruptWrite = true;
    value.items[0].item = 103;
    assert(!again.save(value, files));
    StateJournal<4> afterCorruption;
    assert(afterCorruption.load(key, restored, files) == LoadResult::loaded && restored.items[0].item == 102);
    files.failRead = true;
    assert(afterCorruption.load(key, restored, files) == LoadResult::unavailable);
    const auto writes = files.writes;
    assert(!afterCorruption.save(value, files) && files.writes == writes);
    files.failRead = files.corruptWrite = false;
    // Both files are profile-bound even if copied to another profile's paths.
    Preferences<4> newGame;
    assert(afterCorruption.load({1, 2, 4}, newGame, files) == LoadResult::missing);
    assert(newGame.count == 0 && !newGame.columns);
    assert(afterCorruption.save(newGame, files));
    assert(afterCorruption.load({1, 2, 4}, restored, files) == LoadResult::loaded && restored.count == 0);
}
