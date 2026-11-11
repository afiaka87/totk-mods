// Copyright (c) HanSoloBlaster, afiaka87
#include <cstdio>
#include <cstring>
#include <string>

#include "pure/SwitchFile.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

constexpr const char* kIds[] = {"phantom-foothold", "zonai-hookshot", "fleet-sync", "bivouac"};
constexpr std::size_t kCount = sizeof(kIds) / sizeof(kIds[0]);

std::string narrow(const char16_t* text) {
    std::string out;
    for (; *text; ++text) out.push_back(static_cast<char>(*text));
    return out;
}

}  // namespace

int main() {
    using namespace fieldworks::pure;
    bool on[kCount];

    parseSwitches("", kIds, kCount, on);
    check(!on[0] && !on[1] && !on[2] && !on[3], "an empty file leaves every member off");

    parseSwitches("zonai-hookshot=1\r\nbivouac = 1\n fleet-sync=0\nphantom-foothold=yes\nunknown=1", kIds, kCount, on);
    check(!on[0], "a value other than 0 or 1 leaves the member off");
    check(on[1], "a CRLF line turns a member on");
    check(!on[2], "an explicit 0 keeps a member off");
    check(on[3], "spaces around the key and before the line are ignored");

    parseSwitches("bivouac=1\nbivouac=0\n", kIds, kCount, on);
    check(!on[3], "the last line for a member wins");

    const bool saved[kCount] = {true, false, true, false};
    char text[128];
    const std::size_t size = formatSwitches(kIds, saved, kCount, text, sizeof(text));
    check(std::string(text, size) == "phantom-foothold=1\nzonai-hookshot=0\nfleet-sync=1\nbivouac=0\n",
          "the file holds one line per member in member order");
    parseSwitches({text, size}, kIds, kCount, on);
    check(on[0] && !on[1] && on[2] && !on[3], "a written file reads back unchanged");
    check(formatSwitches(kIds, saved, kCount, text, 20) == 0, "text that does not fit writes nothing");

    char16_t caption[64];
    switchCaption(u"Bivouac", true, Pending::None, caption, 64);
    check(narrow(caption) == "Bivouac: On", "a switch in effect shows its state");
    switchCaption(u"Bivouac", false, Pending::Now, caption, 64);
    check(narrow(caption) == "Bivouac: Off", "a change that applies at once needs no note");
    switchCaption(u"Fleet Sync", false, Pending::Busy, caption, 64);
    check(narrow(caption) == "Fleet Sync: Off when the current action ends",
          "a change that waits for the member to finish says so");
    switchCaption(u"Bivouac", true, Pending::Unavailable, caption, 64);
    check(narrow(caption) == "Bivouac: On (could not start)", "a member that cannot start says so");
    switchCaption(u"Material Columns", true, Pending::None, caption, 12);
    check(narrow(caption) == "Material Co", "a caption is cut to the buffer and terminated");

    if (failures) return 1;
    std::printf("switch file tests passed\n");
    return 0;
}
