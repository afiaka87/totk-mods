// Copyright (c) HanSoloBlaster, afiaka87
#include "Gates.hpp"

#include <lib.hpp>
#include <lib/hook/nx64/impl.hpp>

#include <cstring>

#include "fieldworks/generated/ClimbPatches.hpp"
#include "totk/suite/Member.hpp"

// The suite links with --wrap on these three exlaunch functions, so every member hook reaches the
// wrappers below and the originals are reached through __real_.
extern "C" {
std::uintptr_t __real__ZN3exl4hook4nx644HookEmmb(std::uintptr_t site, std::uintptr_t callback, bool trampoline);
void __real__ZN3exl4hook4nx6410HookInlineEmmb(std::uintptr_t site, std::uintptr_t callback, bool floats);
void __real__ZN3exl4hook4nx6410InitializeEv();

extern const std::uint32_t fieldworks_gate_stubs[];
extern std::uintptr_t fieldworks_gate_targets[];
void fieldworks_gate_return();
}

namespace fieldworks::gates {
namespace {

constexpr unsigned kStubs = 512;
constexpr unsigned kStubWords = 4;

struct Gate {
    std::uintptr_t on;
    std::uintptr_t off;
    std::uint8_t member;
};

Gate g_gates[kStubs];
unsigned g_used = 0;
int g_current = -1;
bool g_innerHook = false;  // HookInline's own Hook call passes straight through
bool g_initialized = false;
Counts g_counts[kMembers];
bool g_overflow[kMembers];

std::uintptr_t stubAt(unsigned index) {
    return reinterpret_cast<std::uintptr_t>(&fieldworks_gate_stubs[index * kStubWords]);
}

bool trampolineFree() {
    return exl::hook::nx64::s_HookEntryIndex + 2 < static_cast<int>(exl::hook::nx64::HookMax);
}

bool gating() { return g_current >= 0 && totk::suite::alwaysOnDepth == 0; }

// The game's words at every hooked site, saved before the hook is written (totk::suite::originalCode).
// Five words cover exlaunch's longest site patch.
constexpr unsigned kRecords = 1024;
constexpr unsigned kRecordWords = 5;
struct CodeRecord {
    std::uintptr_t site;
    std::uint32_t words[kRecordWords];
};
CodeRecord g_records[kRecords];
unsigned g_recordCount = 0;
unsigned g_near = 0, g_far = 0;

void recordSite(std::uintptr_t site) {
    for (unsigned i = 0; i < g_recordCount; ++i)
        if (g_records[i].site == site) return;  // a second hook here: the first record holds the game's words
    if (g_recordCount == kRecords) {
        Logging.Log("[fieldworks] CODE_RECORD_FULL site=%lx; later code checks there read live code", site);
        return;
    }
    CodeRecord record{site, {}};
    for (unsigned i = 0; i < kRecordWords; ++i) record.words[i] = totk::suite::originalCode(site + 4 * i);
    g_records[g_recordCount++] = record;
}

// exlaunch writes one branch when the hook target is in near range, else a longer sequence that
// code checks expecting one word would see as changed code; the boot log reports the split.
void countForm(std::uintptr_t site) {
    const auto word = *reinterpret_cast<const volatile std::uint32_t*>(site);
    ++((word & 0xfc000000u) == 0x14000000u ? g_near : g_far);
}

// A member that cannot fit keeps its remaining hooks uninstalled and all of its gates off.
bool refuse(const char* kind, std::uintptr_t site) {
    if (!g_overflow[g_current])
        Logging.Log("[fieldworks] GATE_FULL member=%d kind=%s site=%lx stubs=%u trampolines=%d/%lu", g_current,
                    kind, site, g_used, exl::hook::nx64::s_HookEntryIndex + 1, exl::hook::nx64::HookMax);
    g_overflow[g_current] = true;
    return true;
}

}  // namespace

void beginMember(std::size_t member) { g_current = member < kMembers ? static_cast<int>(member) : -1; }
void endMember() { g_current = -1; }
bool fits(std::size_t member) { return member < kMembers && !g_overflow[member]; }
Counts counts(std::size_t member) { return member < kMembers ? g_counts[member] : Counts{}; }
unsigned stubsUsed() { return g_used; }
HookForms hookForms() { return {g_near, g_far, g_recordCount}; }

void apply(std::size_t member, bool on) {
    const bool route = on && fits(member);
    for (unsigned i = 0; i < g_used; ++i)
        if (g_gates[i].member == member)
            __atomic_store_n(&fieldworks_gate_targets[i], route ? g_gates[i].on : g_gates[i].off,
                             __ATOMIC_RELEASE);
}

}  // namespace fieldworks::gates

std::uint32_t totk::suite::originalCode(std::uintptr_t address) {
    using namespace fieldworks::gates;
    for (unsigned i = 0; i < g_recordCount; ++i) {
        const CodeRecord& record = g_records[i];
        if (address >= record.site && address < record.site + 4 * kRecordWords)
            return record.words[(address - record.site) / 4];
    }
    return *reinterpret_cast<const volatile std::uint32_t*>(address);
}

extern "C" {

// Read by the stubs in GateStubs.s; every target starts at the original code.
alignas(8) std::uintptr_t fieldworks_gate_targets[512] = {};

void __wrap__ZN3exl4hook4nx6410InitializeEv() {
    using namespace fieldworks::gates;
    if (g_initialized) return;
    g_initialized = true;
    __real__ZN3exl4hook4nx6410InitializeEv();
}

std::uintptr_t __wrap__ZN3exl4hook4nx644HookEmmb(std::uintptr_t site, std::uintptr_t callback, bool trampoline) {
    using namespace fieldworks::gates;
    if (g_innerHook) return __real__ZN3exl4hook4nx644HookEmmb(site, callback, trampoline);
    if (!gating()) {
        if (g_current >= 0) ++g_counts[g_current].alwaysOn;
        recordSite(site);
        const std::uintptr_t original = __real__ZN3exl4hook4nx644HookEmmb(site, callback, trampoline);
        countForm(site);
        return original;
    }
    // An uninstalled hook leaves the game's code at the site, so the site itself is the original.
    if (g_overflow[g_current] || g_used == kStubs || !trampolineFree()) {
        refuse("hook", site);
        return trampoline ? site : 0;
    }
    const unsigned index = g_used++;
    recordSite(site);
    // A replace hook gets a trampoline too, so that switching it off runs the original code.
    const std::uintptr_t original = __real__ZN3exl4hook4nx644HookEmmb(site, stubAt(index), true);
    countForm(site);
    g_gates[index] = {callback, original, static_cast<std::uint8_t>(g_current)};
    fieldworks_gate_targets[index] = original;
    ++g_counts[g_current].gated;
    return trampoline ? original : 0;
}

void __wrap__ZN3exl4hook4nx6410HookInlineEmmb(std::uintptr_t site, std::uintptr_t callback, bool floats) {
    using namespace fieldworks::gates;
    std::uintptr_t installed = callback;
    if (gating()) {
        if (g_overflow[g_current] || g_used == kStubs || !trampolineFree() ||
            exl::hook::nx64::s_InlineEntryIndex >= exl::hook::nx64::InlinePoolCount) {
            refuse("inline", site);
            return;
        }
        const unsigned index = g_used++;
        g_gates[index] = {callback, reinterpret_cast<std::uintptr_t>(&fieldworks_gate_return),
                          static_cast<std::uint8_t>(g_current)};
        fieldworks_gate_targets[index] = g_gates[index].off;
        ++g_counts[g_current].gated;
        installed = stubAt(index);
    } else if (g_current >= 0) {
        ++g_counts[g_current].alwaysOn;
    }
    recordSite(site);
    g_innerHook = true;
    __real__ZN3exl4hook4nx6410HookInlineEmmb(site, installed, floats);
    g_innerHook = false;
    countForm(site);
}

}  // extern "C"

// Phantom Foothold's climbing edits: each gets its own stub that preserves x17 around the switch test.
namespace fieldworks::climb {
namespace {

JIT_CREATE(s_ClimbJit, 0x1000);

constexpr unsigned kStubWords = 16;
std::uint8_t g_on = 0;
bool g_jitReady = false;

constexpr std::uint32_t kSaveX17 = 0xF81F03F1;     // stur x17, [sp, #-16]
constexpr std::uint32_t kRestoreX17 = 0xF85F03F1;  // ldur x17, [sp, #-16]
constexpr std::uint32_t kLoadSwitch = 0x39400231;  // ldrb w17, [x17]
constexpr std::uint32_t kReturn = 0xD65F03C0;      // ret

std::int64_t signExtend(std::uint64_t value, unsigned bits) {
    const std::uint64_t sign = 1ull << (bits - 1);
    return static_cast<std::int64_t>((value ^ sign) - sign);
}

bool nearBranch(std::uintptr_t from, std::uintptr_t to) {
    const std::int64_t delta = static_cast<std::int64_t>(to - from);
    return delta >= -(1ll << 27) && delta < (1ll << 27);
}

std::uint32_t branch(std::uint32_t opcode, std::uintptr_t from, std::uintptr_t to) {
    return opcode | ((static_cast<std::uint32_t>((to - from) >> 2)) & 0x03FFFFFFu);
}

// Writes `word`, which ran at `site`, so that it runs at `out` and then continues at site + 4.
// Returns the number of words written, or zero when a branch cannot reach.
unsigned relocate(std::uint32_t word, std::uintptr_t site, std::uint32_t* rw, std::uintptr_t out) {
    const std::uintptr_t next = site + 4;
    if ((word & 0x7C000000u) == 0x14000000u) {  // B, BL
        const std::uintptr_t target = site + (signExtend(word & 0x03FFFFFFu, 26) << 2);
        if (!nearBranch(out, target) || !nearBranch(out + 4, next)) return 0;
        rw[0] = branch(word & 0xFC000000u, out, target);
        rw[1] = branch(0x14000000u, out + 4, next);
        return 2;
    }
    std::int64_t offset = 0;
    std::uint32_t taken = 0;
    if ((word & 0xFF000010u) == 0x54000000u || (word & 0x7E000000u) == 0x34000000u) {  // B.cond, CBZ/CBNZ
        offset = signExtend((word >> 5) & 0x7FFFFu, 19);
        taken = (word & ~(0x7FFFFu << 5)) | (2u << 5);
    } else if ((word & 0x7E000000u) == 0x36000000u) {  // TBZ/TBNZ
        offset = signExtend((word >> 5) & 0x3FFFu, 14);
        taken = (word & ~(0x3FFFu << 5)) | (2u << 5);
    } else if ((word & 0x9F000000u) == 0x10000000u || (word & 0x9F000000u) == 0x90000000u ||
               (word & 0x3B000000u) == 0x18000000u) {
        return 0;  // ADR, ADRP and literal loads do not occur in the edits
    } else {
        rw[0] = word;
        if (!nearBranch(out + 4, next)) return 0;
        rw[1] = branch(0x14000000u, out + 4, next);
        return 2;
    }
    const std::uintptr_t target = site + (offset << 2);
    if (!nearBranch(out + 4, next) || !nearBranch(out + 8, target)) return 0;
    rw[0] = taken;  // condition true: skip to the third word
    rw[1] = branch(0x14000000u, out + 4, next);
    rw[2] = branch(0x14000000u, out + 8, target);
    return 3;
}

// Two adjacent edits where the second becomes a return: one read of the switch decides both.
bool returnsNext(const VersionPatches& set, std::uint32_t i) {
    return i + 1 < set.count && set.patches[i + 1].offset == set.patches[i].offset + 4 &&
           set.patches[i + 1].replacement == kReturn;
}

// Builds one stub. Returns false when a branch cannot reach.
bool build(const Patch& patch, std::uintptr_t site, std::uint32_t* rw, std::uintptr_t rx, bool thenReturn) {
    constexpr unsigned kLiteral = kStubWords - 2;  // 8-byte aligned: stubs are 64 bytes apart
    rw[0] = kSaveX17;
    rw[1] = 0x58000000u | (((kLiteral - 1) & 0x7FFFFu) << 5) | 17u;  // ldr x17, literal
    rw[2] = kLoadSwitch;
    rw[4] = kRestoreX17;
    unsigned at = 5;
    const unsigned off = relocate(patch.original, site, rw + at, rx + at * 4);
    if (!off) return false;
    at += off;
    const unsigned onAt = at;
    rw[3] = 0x35000000u | (((onAt - 3) & 0x7FFFFu) << 5) | 17u;  // cbnz w17, on
    rw[at++] = kRestoreX17;
    const unsigned on = relocate(patch.replacement, site, rw + at, rx + at * 4);
    if (!on) return false;
    at += on;
    if (thenReturn) {
        if (on != 2) return false;  // only a plain instruction followed by the branch back
        rw[at - 1] = kReturn;
    }
    if (at > kLiteral) return false;
    while (at < kLiteral) rw[at++] = 0xD503201Fu;  // nop
    const std::uint64_t address = reinterpret_cast<std::uintptr_t>(&g_on);
    std::memcpy(rw + kLiteral, &address, sizeof(address));
    return true;
}

const VersionPatches* patchesFor(const char* version) {
    for (const auto& entry : kVersions)
        if (version && std::strcmp(entry.version, version) == 0) return &entry;
    return nullptr;
}

}  // namespace

unsigned install(std::uintptr_t mainBase, const char* version) {
    const VersionPatches* set = patchesFor(version);
    if (!set || !set->count) {
        Logging.Log("[fieldworks] CLIMB_OFF reason=no-edits version=%s", version ? version : "unknown");
        return 0;
    }
    static_assert(sizeof(VersionPatches::patches) / sizeof(Patch) * kStubWords * 4 <= 0x1000);
    for (std::uint32_t i = 0; i < set->count; ++i) {
        const auto& patch = set->patches[i];
        const std::uintptr_t site = mainBase + patch.offset;
        const std::uint32_t found = *reinterpret_cast<const std::uint32_t*>(site);
        if (found != patch.original) {
            Logging.Log("[fieldworks] CLIMB_OFF reason=site-changed offset=%x expected=%08x found=%08x",
                        patch.offset, patch.original, found);
            return 0;
        }
    }
    if (!g_jitReady) {
        s_ClimbJit.Initialize();
        g_jitReady = true;
    }
    auto* rw = reinterpret_cast<std::uint32_t*>(s_ClimbJit.GetRw());
    const std::uintptr_t rx = s_ClimbJit.GetRo();
    for (std::uint32_t i = 0; i < set->count; ++i) {
        const std::uintptr_t site = mainBase + set->patches[i].offset;
        const std::uintptr_t stub = rx + i * kStubWords * 4;
        if (!nearBranch(site, stub) ||
            !build(set->patches[i], site, rw + i * kStubWords, stub, returnsNext(*set, i))) {
            Logging.Log("[fieldworks] CLIMB_OFF reason=out-of-range offset=%x module=%lx", set->patches[i].offset,
                        exl::util::modules::GetSelfStart() - mainBase);
            return 0;
        }
    }
    s_ClimbJit.Flush();
    unsigned paired = 0;
    for (std::uint32_t i = 0; i < set->count; ++i) {
        if (i > 0 && returnsNext(*set, i - 1)) {
            ++paired;
            continue;
        }
        const std::uintptr_t site = mainBase + set->patches[i].offset;
        const std::uintptr_t stub = rx + i * kStubWords * 4;
        const exl::util::RwPages pages(site, sizeof(std::uint32_t));
        *reinterpret_cast<std::uint32_t*>(pages.GetRw()) = branch(0x14000000u, site, stub);
        __builtin___clear_cache(reinterpret_cast<char*>(site), reinterpret_cast<char*>(site + 4));
    }
    Logging.Log("[fieldworks] CLIMB_READY version=%s edits=%u paired=%u", set->version, set->count, paired);
    return set->count;
}

void apply(bool on) { __atomic_store_n(&g_on, on ? 1 : 0, __ATOMIC_RELEASE); }

}  // namespace fieldworks::climb
