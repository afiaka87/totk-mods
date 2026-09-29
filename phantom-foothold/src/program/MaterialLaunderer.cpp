// Phantom Foothold: clears the climb-block bits of collision material sheets as the game loads them.
// The two loaders keep pointers into the raw file buffer, so masking before parsing equals shipping edited data.

#include <lib.hpp>

#include "GameProfiles.hpp"
#include "PhantomFootholdIntegration.hpp"
#include "launder_policy.hpp"

#define PFCLOG(...) Logging.Log("[pfcode] " __VA_ARGS__)

namespace {

namespace policy = phantom::launder;
namespace profiles = phantom::profiles;

bool siteMatches(std::uintptr_t mainBase, ptrdiff_t offset, const u32 (&expected)[5]) {
    const auto& text = exl::util::GetMainModuleInfo().m_Text;
    const std::uintptr_t address = mainBase + offset;
    if (address < text.m_Start || address + sizeof(expected) > text.GetEnd()) { return false; }
    const auto* words = reinterpret_cast<const u32*>(address);
    for (u32 i = 0; i < 5; ++i) {
        if (words[i] != expected[i]) { return false; }
    }
    return true;
}

// Both loader sites must match exactly one profile; otherwise nothing is installed and climbing stays vanilla.
const profiles::GameProfile* selectGameProfile(std::uintptr_t mainBase) {
    const profiles::GameProfile* selected = nullptr;
    for (const auto& profile : profiles::kGameProfiles) {
        if (!siteMatches(mainBase, profile.scDoCreate, profile.scWords)
            || !siteMatches(mainBase, profile.shDoCreate, profile.shWords)) {
            continue;
        }
        if (selected != nullptr) {
            PFCLOG("VERSION GUARD: ambiguous native profile (%s / %s); installing nothing",
                   selected->version, profile.version);
            return nullptr;
        }
        selected = &profile;
    }
    if (selected == nullptr) { PFCLOG("VERSION GUARD: no supported native profile; installing nothing"); }
    return selected;
}

policy::Stats g_sc = {}, g_sh = {};

// Every asset load reaches these hooks, so buffers that are not collision containers pass silently.
void launder(policy::Stats& s, const char* what, unsigned char* buf, u32 bufSize,
             const policy::SheetLocation& sheet) {
    if (sheet.verdict == policy::SheetVerdict::NotAContainer) { return; }
    const int cleared = sheet.verdict == policy::SheetVerdict::Sheet
                            ? policy::maskSheet(buf, bufSize, sheet.offset, sheet.rows)
                            : policy::SHEET_REFUSED;
    const policy::NoteOutcome outcome = policy::note(s, cleared);
    if (!outcome.shouldLog) { return; }
    if (outcome.kind == policy::NoteKind::Refused) {
        PFCLOG("%s: implausible material sheet - left untouched", what);
    } else {
        PFCLOG("%s: cleared %d rows (totals: %u containers / %u rows / %u bails)",
               what, cleared, s.containers, s.rows, s.bails);
    }
}

// World and shrine tiles (bphsc): the fixed1 material palette.
HOOK_DEFINE_TRAMPOLINE(ScDoCreateHook) {
    static u64 Callback(u64 self, unsigned char* buf, u64 size, void* heap) {
        launder(g_sc, "tile", buf, (u32)size, policy::locateTileSheet(buf, size));
        return Orig(self, buf, size, heap);
    }
};

// Object shapes (bphsh), including the shape blobs embedded in tiles.
HOOK_DEFINE_TRAMPOLINE(ShDoCreateHook) {
    static u64 Callback(u64 self, unsigned char* buf, u32 size, void* heap) {
        launder(g_sh, "shape", buf, size, policy::locateShapeSheet(buf, size));
        return Orig(self, buf, size, heap);
    }
};

} // namespace

bool phantom::integration::install(std::uintptr_t mainBase) {
    const auto* profile = selectGameProfile(mainBase);
    if (profile == nullptr) { return false; }
    ScDoCreateHook::InstallAtOffset(profile->scDoCreate);
    ShDoCreateHook::InstallAtOffset(profile->shDoCreate);
    PFCLOG("hooks installed for %s (%s): tile loader + shape loader", profile->version, profile->buildId);
    return true;
}

#ifndef PHANTOM_FOOTHOLD_SUITE_MEMBER
extern "C" void exl_main(void* /*x0*/, void* /*x1*/) {
    exl::hook::Initialize();
    (void)phantom::integration::install(exl::util::modules::GetTargetStart());
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
#endif
