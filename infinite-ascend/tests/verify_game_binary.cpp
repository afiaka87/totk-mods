#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

#include "GameProfiles.hpp"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: zonai_ascend_verify_main <main image> <game version>\n";
        return 2;
    }

    std::ifstream image(argv[1], std::ios::binary | std::ios::ate);
    if (!image) {
        std::cerr << "cannot open main image\n";
        return 2;
    }
    const auto size = static_cast<std::size_t>(image.tellg());
    bool readable = true;
    const auto* game = zonai_ascend::profiles::select(
        size, [&](std::ptrdiff_t offset) {
            std::uint32_t word = 0;
            image.seekg(offset);
            image.read(reinterpret_cast<char*>(&word), sizeof(word));
            if (!image) readable = false;
            return word;
        });
    if (!readable || !game || std::strcmp(game->version, argv[2]) != 0) {
        std::cerr << "main image did not match the requested profile\n";
        return 1;
    }
    std::cout << "PASS TotK " << game->version << ": all "
              << game->sites.size() << " original hook words match\n";
    return 0;
}
