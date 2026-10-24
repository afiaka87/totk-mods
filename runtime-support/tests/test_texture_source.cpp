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
unsigned opens{}, reads{}, closes{}, openError{}, readError{}, failRead{};
void reset(std::size_t size) {
    data.assign(size, 42);
    reportedSize = size;
    opens = reads = closes = openError = readError = failRead = 0;
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
    if (readError || reads==failRead) return {readError ? readError : 1};
    REQUIRE(std::uint64_t(offset)+bytes <= data.size());
    std::memcpy(target,data.data()+offset,bytes);
    return {};
}
void CloseFile(FileHandle) { ++closes; }
}
TEST_CASE("standard CRC and block-aligned image chunks preserve every source byte") {
    const unsigned char vector[]{'1','2','3','4','5','6','7','8','9'};
    CHECK(totk::assets::crc32(vector) == 0xcbf43926u);
    struct Image { unsigned width,height,format,block,blockBytes; };
    for (const auto image : {Image{800,800,0x42,4,8},Image{162,162,0x49,4,8},
                            Image{512,512,0x4B,4,16},Image{144,144,0x25,1,4},
                            Image{17,5,0x0D,1,2}}) {
        const unsigned rowBytes=((image.width+image.block-1)/image.block)*image.blockBytes;
        const unsigned size=rowBytes*((image.height+image.block-1)/image.block);
        reset(size+13);
        for (unsigned i=0;i<data.size();++i) data[i]=static_cast<unsigned char>((i*31+i/251)%256);
        const totk::assets::Slice slice{13,size,totk::assets::crc32({data.data()+13,size})};
        std::vector<unsigned char> received(size);
        unsigned nextY{},uploads{};
        CHECK(totk::assets::withTexture("content:/test",slice,image.width,image.height,image.format,
            [&](const unsigned char* pixels,unsigned y,unsigned rows) {
                CHECK(y==nextY);CHECK(y%image.block==0);
                CHECK((rows%image.block==0 || y+rows==image.height));
                const auto bytes=((rows+image.block-1)/image.block)*rowBytes;
                CHECK(bytes<=totk::assets::kTextureScratchBytes);
                REQUIRE(y+rows<=image.height);
                std::memcpy(received.data()+(y/image.block)*rowBytes,pixels,bytes);
                nextY+=rows;++uploads;
            }));
        CHECK(nextY==image.height);CHECK(uploads>0);
        CHECK(std::memcmp(received.data(),data.data()+13,size)==0);
        CHECK(closes==1);
    }
}
TEST_CASE("missing corrupt and truncated sources never reach GPU and release the scratch lock") {
    reset(16);
    const totk::assets::Slice slice{0,16,totk::assets::crc32(data)};
    unsigned uploads{};
    const auto upload=[&](const unsigned char*,unsigned,unsigned) { ++uploads; };
    SUBCASE("missing") { openError=1; }
    SUBCASE("read error") { readError=1; }
    SUBCASE("negative size") { reportedSize=-1; }
    SUBCASE("truncated") { reportedSize=15; }
    SUBCASE("bad checksum") { data[3]^=1; }
    CHECK_FALSE(totk::assets::withTexture("content:/test",slice,2,2,0x25,upload));
    CHECK(uploads == 0);
    reset(16);
    CHECK(totk::assets::withTexture("content:/test",slice,2,2,0x25,upload));
    CHECK(uploads == 1);
}
TEST_CASE("busy empty and oversized requests do no filesystem work") {
    reset(16);
    const auto upload=[](const unsigned char*,unsigned,unsigned) { FAIL("unexpected upload"); };
    CHECK_FALSE(totk::assets::withTexture("content:/test",{0,0,0},2,2,0x25,upload));
    CHECK_FALSE(totk::assets::withTexture("content:/test",{0,320001,0},2,2,0x25,upload));
    CHECK_FALSE(totk::assets::withTexture("content:/test",{0,16,0},2,2,0xFFFF,upload));
    CHECK_FALSE(totk::assets::withTexture("content:/test",{0,16,0},0,2,0x25,upload));
    CHECK_FALSE(totk::assets::withTexture("content:/test",{0,16,0},2,3,0x25,upload));
    CHECK_FALSE(totk::assets::withTexture("content:/test",{0,16,0},UINT32_MAX,2,0x25,upload));
    totk::assets::textureLock.test_and_set();
    CHECK_FALSE(totk::assets::withTexture("content:/test",{0,16,0},2,2,0x25,upload));
    totk::assets::textureLock.clear();
    CHECK(opens == 0);
}
TEST_CASE("both sidecars use the native content mount") {
    CHECK(std::string_view(totk::assets::kBombTexturePath) == "content:/BotwAbilities/Textures/Bomb.bin");
    CHECK(std::string_view(totk::assets::kCryonisTexturePath) == "content:/BotwAbilities/Textures/Cryonis.bin");
    reset(16);
    unsigned uploads{};
    const auto upload=[&](const unsigned char*,unsigned,unsigned) { ++uploads; };
    const totk::assets::Slice slice{0,16,totk::assets::crc32(data)};
    CHECK(totk::assets::withTexture(totk::assets::kBombTexturePath,slice,2,2,0x25,upload));
    CHECK(totk::assets::withTexture(totk::assets::kCryonisTexturePath,slice,2,2,0x25,upload));
    CHECK(uploads == 2);
}

TEST_CASE("a second-pass read failure refuses the texture and releases the lock") {
    reset(16);
    const totk::assets::Slice slice{0,16,totk::assets::crc32(data)};
    failRead=2;
    unsigned uploads{};
    const auto upload=[&](const unsigned char*,unsigned,unsigned) { ++uploads; };
    CHECK_FALSE(totk::assets::withTexture("content:/test",slice,2,2,0x25,upload));
    CHECK(uploads==0);CHECK(closes==1);
    reset(16);
    CHECK(totk::assets::withTexture("content:/test",slice,2,2,0x25,upload));
    CHECK(uploads==1);
}
