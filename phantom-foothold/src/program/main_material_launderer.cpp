// Phantom Foothold load-time material launderer, TotK 1.2.1 (build 9B4E43650501A4D4): two trampoline hooks
// on the collision-container loaders clear the no-climb flag bits in the raw buffer before parsing.

#include <lib.hpp>

#include "PhantomFootholdIntegration.hpp"
#include "launder_policy.hpp"   // the masking/admission rules, engine-free and host-tested

// Release builds ship WITHOUT the riding classifier logger;
// -DPF_LAUNDERER_DIAG=ON (CMake option) restores it for diagnostic builds.
#define PFCLOG(...) Logging.Log("[pfcode] " __VA_ARGS__)
#ifdef PF_LAUNDERER_DIAG
  #define PFSLOG(...) Logging.Log("[pfscan] " __VA_ARGS__)
#endif

namespace {

namespace policy = phantom::launder;

namespace off {
    // phive::StaticCompoundResource::doCreate_(this, buffer, size, heap) - bphsc parser
    constexpr ptrdiff_t ScDoCreate = 0x00FBB374;
    // phive::ShapeResource::doCreate_(this, buffer, size, heap) - bphsh parser (also
    // reached by the tile-embedded shape creation helper)
    constexpr ptrdiff_t ShDoCreate = 0x00D742B0;
    // per-contact climb-eligibility classifier (the v0.16.0 logger hook)
    constexpr ptrdiff_t ClimbClassify = 0x0161C380;
}

// Version guard: on any mismatch the module installs nothing and logs why (rules in launder_policy.hpp).
u32 siteWord(ptrdiff_t offset) {
    return *(const u32*)(exl::util::modules::GetTargetStart() + offset);
}

void reportGuardRefusal(ptrdiff_t offset, u32 expect, const char* what) {
    PFCLOG("VERSION GUARD: %s site holds %08x (expected %08x) - NOT installing (wrong game "
           "version or conflicting mod); climbing stays vanilla", what, siteWord(offset), expect);
}

policy::Stats g_sc = {}, g_sh = {};

// Fold one sheet result into the running totals, and log the ones policy says are worth a line.
void note(policy::Stats& s, const char* what, int cleared) {
    const policy::NoteOutcome outcome = policy::note(s, cleared);
    if (!outcome.shouldLog) { return; }
    if (outcome.kind == policy::NoteKind::Refused) {
        PFCLOG("%s: implausible material sheet - left untouched", what);
        return;
    }
    PFCLOG("%s: cleared %d rows (totals: %u containers / %u rows / %u bails)",
           what, cleared, s.containers, s.rows, s.bails);
}

// One sheet, located then masked. A buffer that is not one of our containers is passed
// through in silence - every asset load reaches these hooks.
void launder(policy::Stats& s, const char* what, unsigned char* buf, u32 bufSize,
             const policy::SheetLocation& sheet) {
    switch (sheet.verdict) {
    case policy::SheetVerdict::NotAContainer:
        return;
    case policy::SheetVerdict::Malformed:
        note(s, what, policy::SHEET_REFUSED);
        return;
    case policy::SheetVerdict::Sheet:
        note(s, what, policy::maskSheet(buf, bufSize, sheet.offset, sheet.rows));
        return;
    }
}

// ---- tile loader (bphsc): fixed1 palette --------------------------------------------
HOOK_DEFINE_TRAMPOLINE(ScDoCreateHook) {
    static u64 Callback(u64 self, unsigned char* buf, u64 size, void* heap) {
        launder(g_sc, "tile", buf, (u32)size, policy::locateTileSheet(buf, size));
        return Orig(self, buf, size, heap);
    }
};

// Shape loader (bphsh): material section. The same loader also serves shape blobs embedded in tiles.
HOOK_DEFINE_TRAMPOLINE(ShDoCreateHook) {
    static u64 Callback(u64 self, unsigned char* buf, u32 size, void* heap) {
        launder(g_sh, "shape", buf, size, policy::locateShapeSheet(buf, size));
        return Orig(self, buf, size, heap);
    }
};

// ---- riding diagnostic: the v0.16.0 climb-classifier logger (DIAG builds only) ------
#ifdef PF_LAUNDERER_DIAG
constexpr ptrdiff_t SLOT_BLOCK_BASE   = 688;
constexpr ptrdiff_t SLOT_BLOCK_STRIDE = 28;
constexpr ptrdiff_t SLOT_VERDICT      = 25;

inline int cm(float meters) { return (int)(meters * 100.0f + (meters >= 0.0f ? 0.5f : -0.5f)); }

u64 g_seen[128] = {};
u32 g_evals = 0, g_lines = 0;

void logClassification(u64 rec, u32 slot, const float* pos, const float* norm,
                       const unsigned char* mat, int layer, u32 flags) {
    g_evals++;
    const u32 idx = (slot > 15) ? 0 : slot;
    const unsigned char verdict =
        *(const unsigned char*)(rec + SLOT_BLOCK_BASE + SLOT_BLOCK_STRIDE * (u64)idx + SLOT_VERDICT);
    const unsigned char matbyte = mat[8];
    const int qx = (int)pos[0], qy = (int)pos[1], qz = (int)pos[2];
    u64 key = 1469598103934665603ull;
    const u32 parts[6] = { (u32)qx, (u32)qy, (u32)qz,
                           (u32)(u8)layer | ((u32)matbyte << 8), flags & 1, verdict & 1u };
    for (u32 p : parts) { key ^= p; key *= 1099511628211ull; }
    if (key == 0) { key = 1; }
    const u32 h = (u32)(key ^ (key >> 32)) & 127;
    if (g_seen[h] == key) { return; }
    g_seen[h] = key;
    { static bool once = false;
      if (!once) { once = true; PFSLOG("classifier hook FIRING (first evaluation seen)"); } }
    const u32* w = (const u32*)(mat - 0x18);
    PFSLOG("s=%u ok=%u L=%d m=%02x f=%u p=(%d,%d,%d)cm n=(%d,%d,%d)mil "
           "w=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
           slot, (u32)(verdict & 1u), layer, (u32)matbyte, flags & 1,
           cm(pos[0]), cm(pos[1]), cm(pos[2]),
           (int)(norm[0] * 1000.0f), (int)(norm[1] * 1000.0f), (int)(norm[2] * 1000.0f),
           w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9]);
    if ((++g_lines & 0xFF) == 0) { PFSLOG("counters: %u lines / %u evaluations", g_lines, g_evals); }
}

HOOK_DEFINE_TRAMPOLINE(ClimbClassifyHook) {
    static u64 Callback(u64 rec, u32 slot, const float* pos, const float* norm,
                        const unsigned char* mat, int layer, u32 flags) {
        const u64 ret = Orig(rec, slot, pos, norm, mat, layer, flags);
        logClassification(rec, slot, pos, norm, mat, layer, flags);
        return ret;
    }
};
#endif // PF_LAUNDERER_DIAG

} // namespace

// Entry point
bool phantom::integration::install(std::uintptr_t mainBase) {
    PFCLOG("exl_main begin base=%p (PF material launderer - code edition candidate)",
           (void*)mainBase);
    // All-or-nothing: both loader sites must byte-verify or nothing installs; both words are read before deciding.
    switch (policy::evaluateGuard(siteWord(off::ScDoCreate), siteWord(off::ShDoCreate))) {
    case policy::GuardOutcome::RefusedTileLoader:
        reportGuardRefusal(off::ScDoCreate, policy::EXPECT_TILE_LOADER, "tile loader");
        return false;
    case policy::GuardOutcome::RefusedShapeLoader:
        reportGuardRefusal(off::ShDoCreate, policy::EXPECT_SHAPE_LOADER, "shape loader");
        return false;
    case policy::GuardOutcome::Install:
        break;
    }
    ScDoCreateHook::InstallAtOffset(off::ScDoCreate);
    ShDoCreateHook::InstallAtOffset(off::ShDoCreate);
#ifdef PF_LAUNDERER_DIAG
    if (siteWord(off::ClimbClassify) == policy::EXPECT_CLASSIFIER) {
        ClimbClassifyHook::InstallAtOffset(off::ClimbClassify);
    } else {
        reportGuardRefusal(off::ClimbClassify, policy::EXPECT_CLASSIFIER, "classifier logger");
    }
    PFCLOG("hooks installed: tile loader + shape loader + classifier logger (DIAG build)");
#else
    PFCLOG("hooks installed: tile loader + shape loader");
#endif
    return true;
}

#ifndef PHANTOM_FOOTHOLD_SUITE_MEMBER
extern "C" void exl_main(void* /*x0*/, void* /*x1*/) {
    exl::hook::Initialize();
    (void)phantom::integration::install(exl::util::modules::GetTargetStart());
}

// Referenced by exlaunch's crt0 (the module is never entered as a process; unreachable).
extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
#endif
