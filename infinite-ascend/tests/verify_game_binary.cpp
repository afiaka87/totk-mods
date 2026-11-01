#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

#include "GameProfiles.hpp"
#include "RemoteProfile121.hpp"

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
    if (std::strcmp(game->version, "1.2.1") == 0) {
        for (const auto& guard : zonai_ascend::remote::profile121::kGuards) {
            std::uint32_t word = 0;
            image.seekg(guard.offset);
            image.read(reinterpret_cast<char*>(&word), sizeof(word));
            if (!image || word != guard.original) {
                std::cerr << "remote guard mismatch at " << std::hex << guard.offset << '\n';
                return 1;
            }
        }
        std::cout << "PASS: " << zonai_ascend::remote::profile121::kGuards.size()
                  << " remote-target native guards match\n";
    }
    std::cout << "PASS TotK " << game->version << ": all "
              << game->sites.size() << " original hook words match\n";
    return 0;
}
