// Copyright (c) HanSoloBlaster, afiaka87
// Runs one shared-file merge on files from disk, or prints a packed file's unpacked size.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "pure/merge/GameData.hpp"
#include "pure/merge/Messages.hpp"
#include "pure/merge/Rows.hpp"
#include "pure/merge/SizeTable.hpp"
#include "pure/merge/SoundBank.hpp"
#include "pure/merge/Tag.hpp"
#include "pure/merge/Zstd.hpp"

using namespace fieldworks::merge;

static bool load(const char* path, std::vector<u8>& out) {
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    out.resize(static_cast<std::size_t>(std::ftell(file)));
    std::fseek(file, 0, SEEK_SET);
    const bool ok = std::fread(out.data(), 1, out.size(), file) == out.size();
    std::fclose(file);
    return ok;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "size") {
        std::vector<u8> packed;
        u64 size = 0;
        if (!load(argv[2], packed)) return std::puts("unreadable"), 2;
        const std::size_t head = packed.size() < kFrameHeadBytes ? packed.size() : kFrameHeadBytes;
        if (!frameContentSize(packed.data(), head, size)) return std::puts("unknown"), 1;
        return std::printf("%llu\n", static_cast<unsigned long long>(size)), 0;
    }
    if (argc < 6) return std::fprintf(stderr, "usage: merge_tool <kind> <file> <additions> <output> <spare>\n"), 2;
    const std::string kind = argv[1];
    std::vector<u8> file, delta;
    if (!load(argv[2], file) || !load(argv[3], delta)) return std::fprintf(stderr, "cannot read the inputs\n"), 2;
    const std::size_t used = file.size(), spare = std::strtoull(argv[5], nullptr, 0);
    file.resize(used + spare, 0xcd);
    Work work(file.data(), used, file.size());
    Report report;
    bool ok = false;
    // As in the game: the additions are read into the spare room itself (the sounds are read in pieces).
    u8* held = kind == "sounds" ? nullptr : work.scratch(delta.size());
    if (kind != "sounds" && !held) return std::fprintf(stderr, "no room for the additions\n"), 1;
    if (held) std::memcpy(held, delta.data(), delta.size());
    if (kind == "rows") ok = mergeRows(work, held, delta.size(), report);
    else if (kind == "gamedata") ok = mergeGameData(work, held, delta.size(), report);
    else if (kind == "tag") ok = mergeTag(work, held, delta.size(), report);
    else if (kind == "messages") ok = mergeMessages(work, held, delta.size(), report);
    else if (kind == "sounds") {
        const Source source{[](void* context, u64 offset, void* out, std::size_t bytes) {
            const auto& all = *static_cast<std::vector<u8>*>(context);
            if (offset > all.size() || bytes > all.size() - offset) return false;
            std::memcpy(out, all.data() + offset, bytes);
            return true;
        }, &delta};
        ok = mergeSounds(work, source, delta.size(), report);
    } else if (kind == "table") {
        std::vector<std::string> names;
        std::vector<Budget> budgets;
        for (int i = 6; i < argc; ++i) names.emplace_back(argv[i]);
        for (auto& name : names) {
            const auto at = name.find('=');
            if (at == std::string::npos) return 2;
            name[at] = 0;
            budgets.push_back({name.c_str(), static_cast<u32>(std::strtoul(name.c_str() + at + 1, nullptr, 0))});
        }
        ok = mergeSizeTable(work, held, delta.size(), budgets.data(), static_cast<u32>(budgets.size()), report);
    }
    else return std::fprintf(stderr, "unknown kind %s\n", kind.c_str()), 2;
    std::printf("%s %s added=%u present=%u strings=%u skipped=%u size=%zu->%zu%s%s\n", kind.c_str(),
                ok ? "ok" : "REFUSED", report.added, report.present, report.strings, report.skipped, used, work.size,
                work.refusal ? " reason=" : "", work.refusal ? work.refusal : "");
    std::FILE* out = std::fopen(argv[4], "wb");
    if (!out || std::fwrite(file.data(), 1, work.size, out) != work.size) return 2;
    std::fclose(out);
    return ok ? 0 : 1;
}
