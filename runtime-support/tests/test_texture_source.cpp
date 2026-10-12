// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include "totk/render/TextureSource.hpp"
#include <vector>
#include <cstring>
#include <string_view>
namespace {
std::vector<unsigned char> data;
s64 reportedSize{};
unsigned opens{}, reads{}, closes{}, openError{}, readError{};
void reset(std::size_t size) {
    data.assign(size, 42);
    reportedSize = size;
    opens = reads = closes = openError = readError = 0;
}
}
namespace nn::fs {
Result OpenFile(FileHandle*, const char* path, int mode) {
    REQUIRE(std::string_view(path).starts_with("content:/"));
    CHECK(mode == OpenMode_Read);
    ++opens;
    return {openError};
}
Result GetFileSize(s64* size, FileHandle) { *size=reportedSize; return {}; }
Result ReadFile(FileHandle, s64 offset, void* target, std::uint64_t bytes) {
    ++reads;
    if (readError) return {readError};
    REQUIRE(std::uint64_t(offset)+bytes <= data.size());
    std::memcpy(target,data.data()+offset,bytes);
    return {};
}
void CloseFile(FileHandle) { ++closes; }
}
TEST_CASE("standard CRC and largest image survive exact upload") {
    const unsigned char vector[]{'1','2','3','4','5','6','7','8','9'};
    CHECK(totk::assets::crc32(vector) == 0xcbf43926u);
    reset(totk::assets::kTextureScratchBytes);
    unsigned uploads{};
    CHECK(totk::assets::withTexture("content:/test", {0,unsigned(data.size()),totk::assets::crc32(data)},
        [&](const unsigned char* pixels) { ++uploads; CHECK(std::memcmp(pixels,data.data(),data.size()) == 0); }));
    CHECK(uploads == 1);
    CHECK(closes == 1);
}
TEST_CASE("missing corrupt and truncated sources never reach GPU and release the scratch lock") {
    reset(16);
    const totk::assets::Slice slice{0,16,totk::assets::crc32(data)};
    unsigned uploads{};
    const auto upload=[&](const unsigned char*) { ++uploads; };
    SUBCASE("missing") { openError=1; }
    SUBCASE("read error") { readError=1; }
    SUBCASE("negative size") { reportedSize=-1; }
    SUBCASE("truncated") { reportedSize=15; }
    SUBCASE("bad checksum") { data[3]^=1; }
    CHECK_FALSE(totk::assets::withTexture("content:/test",slice,upload));
    CHECK(uploads == 0);
    reset(16);
    CHECK(totk::assets::withTexture("content:/test",slice,upload));
    CHECK(uploads == 1);
}
TEST_CASE("busy empty and oversized requests do no filesystem work") {
    reset(16);
    const auto upload=[](const unsigned char*) { FAIL("unexpected upload"); };
    CHECK_FALSE(totk::assets::withTexture("content:/test",{0,0,0},upload));
    CHECK_FALSE(totk::assets::withTexture("content:/test",{0,320001,0},upload));
    totk::assets::textureLock.test_and_set();
    CHECK_FALSE(totk::assets::withTexture("content:/test",{0,16,0},upload));
    totk::assets::textureLock.clear();
    CHECK(opens == 0);
}
TEST_CASE("both sidecars use the native content mount") {
    CHECK(std::string_view(totk::assets::kBombTexturePath) == "content:/BotwAbilities/Textures/Bomb.bin");
    CHECK(std::string_view(totk::assets::kCryonisTexturePath) == "content:/BotwAbilities/Textures/Cryonis.bin");
    reset(16);
    unsigned uploads{};
    const auto upload=[&](const unsigned char*) { ++uploads; };
    const totk::assets::Slice slice{0,16,totk::assets::crc32(data)};
    CHECK(totk::assets::withTexture(totk::assets::kBombTexturePath,slice,upload));
    CHECK(totk::assets::withTexture(totk::assets::kCryonisTexturePath,slice,upload));
    CHECK(uploads == 2);
}
