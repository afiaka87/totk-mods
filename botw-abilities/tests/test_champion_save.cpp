// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "SavedState.hpp"
#include "SaveBuffer.hpp"
#include "StateTransfer.hpp"
#include <algorithm>
#include <limits>
#include <thread>
#include <vector>

using namespace botw_abilities::champions;
namespace {
struct Keys { std::uint32_t enabled, charges, recharge; };
constexpr std::array<Keys, 4> keys{{{1, 11, 21}, {2, 12, 22}, {3, 13, 23}, {4, 14, 24}}};
States full() {
    return {initial(Blessing::Mipha), initial(Blessing::Revali),
            initial(Blessing::Daruk), initial(Blessing::Urbosa)};
}
void put(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, 4);
}
std::vector<unsigned char> serialized() {
    std::vector<unsigned char> bytes(0x20, 0xAC);
    const auto append = [&](std::uint32_t hash, std::uint32_t value) {
        const auto at = bytes.size(); bytes.resize(at + 8);
        put(bytes, at, hash); put(bytes, at + 4, value);
    };
    for (unsigned type : {0u, 2u, 4u}) {
        append(0, type);
        append(0xF0000000u + type, 0xCCDD0011u);
        for (std::size_t i = 0; i < keys.size(); ++i)
            append(type == 0 ? keys[i].enabled : type == 2 ? keys[i].charges : keys[i].recharge,
                   type == 0 ? 1 : type == 2 ? kRules[i].charges : 0);
    }
    put(bytes, 0, 0x01020304); put(bytes, 8, static_cast<unsigned>(bytes.size()));
    bytes.resize(bytes.size() + 39, 0xA5);
    return bytes;
}
struct FakeGameData {
    States values = full();
    std::array<int, 3> capacity{4, 4, 4}, requested{};
    std::vector<std::uint32_t> writes;
    unsigned missing = 0;
    int invalidCharges = -1;
    bool boolean(std::uint32_t hash, bool& value) {
        if (missing == hash) return false;
        value = values[hash - 1].enabled; return true;
    }
    bool integer(std::uint32_t hash, std::int32_t& value) {
        if (missing == hash) return false;
        value = invalidCharges >= 0 ? invalidCharges : values[hash - 11].charges; return true;
    }
    bool real(std::uint32_t hash, float& value) {
        if (missing == hash) return false;
        value = values[hash - 21].remainingSeconds; return true;
    }
    struct Output {
        FakeGameData& data;
        bool room(const std::array<int, 3>& pushes) {
            data.requested = pushes;
            for (std::size_t i = 0; i < pushes.size(); ++i)
                if (pushes[i] > data.capacity[i]) return false;
            return true;
        }
        void boolean(std::uint32_t hash, bool value) {
            data.writes.push_back(hash); data.values[hash - 1].enabled = value;
        }
        void integer(std::uint32_t hash, std::int32_t value) {
            data.writes.push_back(hash); data.values[hash - 11].charges = static_cast<std::uint8_t>(value);
        }
        void real(std::uint32_t hash, float value) {
            data.writes.push_back(hash); data.values[hash - 21].remainingSeconds = value;
        }
    };
};
}

TEST_CASE("Champion menu changes belong to the selected save generation") {
    SavedState state;
    REQUIRE(state.restore(0, full()));
    REQUIRE(state.enable(0, Blessing::Revali, false));
    CHECK_FALSE(state.snapshot().states[1].enabled);
    const auto second = state.invalidate();
    CHECK_FALSE(state.snapshot().ready);
    CHECK_FALSE(state.enable(0, Blessing::Daruk, false));
    CHECK_FALSE(state.restore(0, full()));
    auto loaded = full(); loaded[2].charges = 2;
    REQUIRE(state.restore(second, loaded));
    CHECK(state.snapshot().states[1].enabled);
    CHECK(state.snapshot().states[2].charges == 2);
    CHECK_FALSE(state.restore(second, full()));
}

TEST_CASE("Champion use is atomic across gameplay callbacks") {
    SavedState state;
    REQUIRE(state.restore(0, full()));
    std::atomic<unsigned> uses{};
    const auto hit = [&] {
        for (int i = 0; i < 12; ++i)
            if (state.use(Blessing::Daruk, {true}).consumed) ++uses;
    };
    std::thread first(hit), second(hit);
    first.join(); second.join();
    CHECK(uses.load() == 6);
    CHECK(state.snapshot().states[2].remainingSeconds == 1080.0f);
    std::array<Context, 4> paused{};
    CHECK(state.tick(paused, 120) == 0);
    CHECK(state.snapshot().states[2].remainingSeconds == 1080.0f);
    paused[2] = {true, false, true, true};
    CHECK(state.tick(paused, 120) == 4);
    CHECK(state.snapshot().states[2].charges == 6);
}

TEST_CASE("Saving directly from the menu writes only the twelve Champion scalar values") {
    SavedState state;
    REQUIRE(state.restore(0, full()));
    REQUIRE(state.enable(0, Blessing::Mipha, false));
    REQUIRE(state.use(Blessing::Urbosa, {true}).consumed);
    auto bytes = serialized(); const auto before = bytes;
    REQUIRE(patchSaveBuffer(bytes.data(), bytes.size(), keys, state.snapshot()) == SavePatch::Applied);
    std::vector<std::size_t> owned;
    for (std::size_t at = 0x20; at < saveWord(bytes.data(), 8); at += 8) {
        const auto hash = saveWord(bytes.data(), at);
        for (const auto& key : keys)
            if (hash == key.enabled || hash == key.charges || hash == key.recharge)
                for (std::size_t byte = at + 4; byte < at + 8; ++byte) owned.push_back(byte);
        if (hash == keys[0].enabled) CHECK(saveWord(bytes.data(), at + 4) == 0);
        if (hash == keys[3].charges) CHECK(saveWord(bytes.data(), at + 4) == 2);
    }
    for (std::size_t i = 0; i < bytes.size(); ++i)
        if (std::find(owned.begin(), owned.end(), i) == owned.end()) CHECK(bytes[i] == before[i]);
}

TEST_CASE("Champion save refuses malformed, missing, duplicate and wrongly typed fields without edits") {
    SavedSnapshot snapshot{full(), 1, true};
    auto bytes = serialized();
    SavePatch expected = SavePatch::InvalidBuffer;
    SUBCASE("bad header") { put(bytes, 0, 0); }
    SUBCASE("table extends past allocation") { put(bytes, 8, 0xFFFF); }
    SUBCASE("unaligned table") { put(bytes, 8, saveWord(bytes.data(), 8) - 1); }
    SUBCASE("missing last private field") {
        put(bytes, 0x20 + 17 * 8, 0xFADE); expected = SavePatch::MissingFields;
    }
    SUBCASE("duplicate replaces unrelated field") {
        put(bytes, 0x28, keys[0].enabled); expected = SavePatch::DuplicateField;
    }
    SUBCASE("bool is actually a bool array offset") {
        put(bytes, 0x24, 1); expected = SavePatch::WrongType;
    }
    SUBCASE("unavailable save") { snapshot.ready = false; expected = SavePatch::NotReady; }
    SUBCASE("NaN timer") {
        snapshot.states[0].remainingSeconds = std::numeric_limits<float>::quiet_NaN();
        expected = SavePatch::NotReady;
    }
    const auto original = bytes;
    CHECK(patchSaveBuffer(bytes.data(), bytes.size(), keys, snapshot) == expected);
    CHECK(bytes == original);
}

TEST_CASE("Champion GameData restore never publishes a partial or truncated state") {
    FakeGameData game;
    States output = full(); output[1].enabled = false;
    SUBCASE("missing field") { game.missing = keys[3].recharge; }
    SUBCASE("charge would wrap to valid byte") { game.invalidCharges = 257; }
    SUBCASE("inconsistent exhausted state") { game.values[0].charges = 0; }
    SUBCASE("nonfinite timer") { game.values[2].remainingSeconds = std::numeric_limits<float>::infinity(); }
    CHECK(readStates(keys, game, output) != StateTransfer::Ready);
    CHECK_FALSE(output[1].enabled);
    CHECK(output[0].charges == 1);
}

TEST_CASE("Champion GameData writes preflight every queue and retry the whole transition") {
    FakeGameData game;
    FakeGameData::Output writer{game};
    SavedSnapshot desired{full(), 1, true};
    desired.states[0] = {0, 1440, false};
    const auto before = game.values;
    CHECK(writeStates(keys, writer, before, desired, false) == StateTransfer::NotReady);
    CHECK(game.writes.empty());
    game.capacity[2] = 0;
    CHECK(writeStates(keys, writer, before, desired, true) == StateTransfer::QueueUnavailable);
    CHECK(game.requested == std::array<int, 3>{1, 1, 1});
    CHECK(game.writes.empty());
    game.capacity[2] = 4;
    REQUIRE(writeStates(keys, writer, before, desired, true) == StateTransfer::Ready);
    CHECK(game.writes == std::vector<std::uint32_t>{1, 11, 21});
    game.writes.clear();
    REQUIRE(writeStates(keys, writer, game.values, desired, true) == StateTransfer::Ready);
    CHECK(game.writes.empty());
}
