// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/native/GameVersion.hpp"
#include <cassert>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
using namespace material_columns::native;
int main(int argc, char** argv) {
    std::vector<std::uint32_t> memory(0x5000000 / 4);
    const auto base = reinterpret_cast<std::uintptr_t>(memory.data());
    const auto bytes = memory.size() * 4;
    assert(!selectNativeVersion(0, bytes, bytes));
    assert(!selectNativeVersion(base, bytes, bytes));
    assert(nativeOffset(0x123a220) == 0);
    memory[0x123a220/4]=0xa9bb7bfd; memory[0x123a220/4+1]=0xf9000bf9;
    memory[0x2a66f00/4]=0xa9bd7bfd; memory[0x2a66f00/4+1]=0xf9000bf5;
    assert(selectNativeVersion(base, bytes, bytes) && !is142());
    assert(nativeOffset(0x123a220)==0x123a220 && nativeFrameOffset()==8);
    assert(!selectNativeVersion(base, 4, bytes));
    assert(!selectNativeVersion(base, bytes, 4));
    for (const auto& entry : native142Entries)
        if (entry.source==0x123a220 || entry.source==0x2a66f00)
            std::memcpy(memory.data()+entry.target/4,entry.words,8);
    assert(!selectNativeVersion(base, bytes, bytes)); // Two matching builds must be rejected.
    memory[0x123a220/4]=0;
    assert(selectNativeVersion(base, bytes, bytes) && is142());
    assert(nativeFrameOffset()==16);
    for (const auto& entry : native142Entries) assert(nativeOffset(entry.source)==entry.target);
    memory[0xc73118/4]^=1;
    assert(!selectNativeVersion(base, bytes, bytes));
    for (int i=1;i<argc;++i) {
        std::fill(memory.begin(),memory.end(),0);
        std::ifstream file(argv[i],std::ios::binary|std::ios::ate);
        assert(file);
        const auto size=file.tellg(); assert(size>0 && static_cast<std::size_t>(size)<=bytes);
        file.seekg(0);file.read(reinterpret_cast<char*>(memory.data()),size);assert(file);
        assert(selectNativeVersion(base,static_cast<std::size_t>(size),bytes));
        assert(nativeVersion==(i==1 ? NativeVersion::V121 : NativeVersion::V142));
    }
}
