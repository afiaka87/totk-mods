// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include <doctest.h>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "CarrierSaveFilter.hpp"

using namespace arrowbound::pure;
namespace {
void word(std::vector<unsigned char>& data, std::size_t offset, std::uint32_t value) {
    std::memcpy(data.data() + offset, &value, 4);
}

struct Fixture {
    static constexpr std::size_t names = 0x80, stocks = names + 4 + 200 * 64;
    std::vector<unsigned char> data = std::vector<unsigned char>(stocks + 4 + 200 * 4 + 16);
    Fixture() {
        word(data, 0, 0x01020304);
        word(data, 8, 0x38);
        word(data, 0x20, kWellQuestHash);
        word(data, 0x24, kWellSearch);
        word(data, 0x28, kKeyItemNamesHash);
        word(data, 0x2c, static_cast<std::uint32_t>(names));
        word(data, 0x30, kKeyItemStocksHash);
        word(data, 0x34, static_cast<std::uint32_t>(stocks));
        word(data, names, 200);
        word(data, stocks, 200);
        setName(0, "Obj_ProofKorok");
        setName(1, kSavedCarrierName);
        setName(199, "Obj_ProofKorok");
        word(data, stocks + 4, 12);
        word(data, stocks + 8, 0xffffffff);
        word(data, stocks + 4 + 199 * 4, 9);
    }
    void setName(std::size_t slot, const char* name) {
        std::memset(data.data() + names + 4 + slot * 64, 0, 64);
        std::memcpy(data.data() + names + 4 + slot * 64, name, std::strlen(name) + 1);
    }
    auto filter() { return filterCarrierSave(data.data(), data.size()); }
};
}

TEST_CASE("only the unearned emblem name and stock are excluded from saves") {
    for (const auto step : {kWellNotReady, kWellReady, kWellSearch}) {
        Fixture f;
        word(f.data, 0x24, step);
        auto expected = f.data;
        std::memset(expected.data() + f.names + 4 + 64, 0, 64);
        word(expected, f.stocks + 8, 0);
        CHECK(f.filter() == CarrierSaveResult::Removed);
        CHECK(f.data == expected);
        CHECK(f.filter() == CarrierSaveResult::Absent);
    }
}

TEST_CASE("earned and unknown quest states preserve the entire save") {
    for (const auto step : {kWellComplete, 0u, 0xdeadbeefu}) {
        Fixture f;
        word(f.data, 0x24, step);
        const auto before = f.data;
        CHECK(f.filter() == (step == kWellComplete ? CarrierSaveResult::Earned :
                                                     CarrierSaveResult::UnknownQuest));
        CHECK(f.data == before);
    }
}

TEST_CASE("malformed save layouts are refused before any bytes change") {
    for (int fault = 0; fault < 12; ++fault) {
        Fixture f;
        switch (fault) {
            case 0: word(f.data, 0, 0); break;
            case 1: word(f.data, 8, 0xffffffff); break;
            case 2: word(f.data, 8, 0x37); break;
            case 3: word(f.data, 0x2c, 0x20); break;
            case 4: word(f.data, 0x2c, 0xffffffff); break;
            case 5: word(f.data, 0x34, static_cast<std::uint32_t>(f.names)); break;
            case 6: word(f.data, f.names, 199); break;
            case 7: word(f.data, f.stocks, 201); break;
            case 8: std::memset(f.data.data() + f.names + 4 + 199 * 64, 'x', 64); break;
            case 9: word(f.data, 0x30, kKeyItemNamesHash); break;
            case 10: word(f.data, 0x28, 0); break;
            case 11: f.data.resize(f.stocks + 8); break;
        }
        const auto before = f.data;
        CHECK(f.filter() == CarrierSaveResult::Invalid);
        CHECK(f.data == before);
    }
    CHECK(filterCarrierSave(nullptr, 0) == CarrierSaveResult::Invalid);
}

TEST_CASE("all injected duplicates are removed while similar actor names stay intact") {
    Fixture f;
    f.setName(199, kSavedCarrierName);
    f.setName(2, "Obj_CaveWellHonor_001");
    CHECK(f.filter() == CarrierSaveResult::Removed);
    CHECK(f.data[f.names + 4 + 64] == 0);
    CHECK(f.data[f.names + 4 + 199 * 64] == 0);
    CHECK(std::strcmp(reinterpret_cast<char*>(f.data.data() + f.names + 4 + 2 * 64),
                      "Obj_CaveWellHonor_001") == 0);
}

TEST_CASE("local save copy validates real layout and earned versus unearned cases in memory") {
    std::string fixturePath;
#ifdef _MSC_VER
    char* path = nullptr;
    std::size_t length = 0;
    REQUIRE(_dupenv_s(&path, &length, "ARROWBOUND_SAVE_FIXTURE") == 0);
    if (!path) return;
    fixturePath = path;
    std::free(path);
#else
    const char* path = std::getenv("ARROWBOUND_SAVE_FIXTURE");
    if (!path) return;
    fixturePath = path;
#endif
    // Private fixture is never checked in or written by this test.
    std::ifstream file(fixturePath, std::ios::binary);
    REQUIRE(file.good());
    const std::vector<unsigned char> original{std::istreambuf_iterator<char>(file),
                                             std::istreambuf_iterator<char>()};
    REQUIRE(original.size() > 0x28);
    const auto end = saveWord(original.data(), 8);
    REQUIRE(end < original.size());
    std::size_t quest = 0, names = 0, stocks = 0;
    for (std::size_t off = 0x20; off < end; off += 8) {
        const auto hash = saveWord(original.data(), off);
        if (hash == kWellQuestHash) quest = off + 4;
        if (hash == kKeyItemNamesHash) names = saveWord(original.data(), off + 4);
        if (hash == kKeyItemStocksHash) stocks = saveWord(original.data(), off + 4);
    }
    REQUIRE(quest != 0);
    REQUIRE(names != 0);
    REQUIRE(stocks != 0);
    REQUIRE(saveWord(original.data(), names) == 200);
    REQUIRE(saveWord(original.data(), stocks) == 200);
    auto unearned = original;
    word(unearned, quest, kWellSearch);
    const auto slotName = names + 4 + 199 * 64;
    const auto slotStock = stocks + 4 + 199 * 4;
    std::memset(unearned.data() + slotName, 0, 64);
    std::memcpy(unearned.data() + slotName, kSavedCarrierName, sizeof(kSavedCarrierName));
    word(unearned, slotStock, 0xffffffff);
    auto earned = unearned;
    word(earned, quest, kWellComplete);
    const auto earnedBefore = earned;
    CHECK(filterCarrierSave(earned.data(), earned.size()) == CarrierSaveResult::Earned);
    CHECK(earned == earnedBefore);
    auto expected = unearned;
    for (unsigned i = 0; i < 200; ++i) {
        const auto name = names + 4 + i * 64;
        if (std::strcmp(reinterpret_cast<char*>(expected.data() + name), kSavedCarrierName) == 0) {
            std::memset(expected.data() + name, 0, 64);
            word(expected, stocks + 4 + i * 4, 0);
        }
    }
    CHECK(filterCarrierSave(unearned.data(), unearned.size()) == CarrierSaveResult::Removed);
    CHECK(unearned == expected);
}
