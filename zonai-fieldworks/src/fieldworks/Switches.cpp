// Copyright (c) HanSoloBlaster, afiaka87
#include "Switches.hpp"

#include <lib.hpp>
#include <nn/fs.h>

#include "fieldworks/Gates.hpp"
#include "pure/SwitchFile.hpp"
#include "totk/engine/SdCardMount.hpp"

namespace fieldworks::switches {
namespace {

using gates::kMembers;

constexpr char kDirectory[] = "sdcard:/ZonaiFieldworks";
constexpr std::uint32_t kPathNotFound = 0x202, kPathAlreadyExists = 0x402;  // fs module 2
constexpr unsigned kRetryTicks = 30, kTries = 20;
// Updates (about a second) a member's hooks keep running after it is told it is off, for cleanup.
constexpr unsigned kStopTicks = 60;
constexpr std::size_t kPhantom = 0, kBotw = 4;

const totk::suite::Member* g_members{};
const char* const* g_icons{};
bool g_supported[kMembers]{};
const char* g_ids[kMembers]{};
std::size_t g_count = 0;

// Hooks routed to the member's code (On, Stopping) or to the game's own code (Off).
enum class Phase : unsigned char { Off, On, Stopping };

bool g_saved[kMembers]{};  // what the file says now, including the player's changes this run
Phase g_phase[kMembers]{};
unsigned g_stopTicks[kMembers]{};
bool g_waitLogged[kMembers]{};
bool g_read = false;
unsigned g_ticks = 0, g_readTries = 0, g_writeTries = 0;
bool g_dirty = false;
char16_t g_captions[kMembers][64];

enum class Read { Present, Missing, Retry };

Read readFile() {
    if (totk::engine::ensureSdCardMounted() == totk::engine::SdCardMountResult::Failed) return Read::Retry;
    nn::fs::FileHandle file;
    auto result = nn::fs::OpenFile(&file, kFile, nn::fs::OpenMode_Read);
    if (result.IsFailure())
        return result.GetInnerValueForDebug() == kPathNotFound ? Read::Missing : Read::Retry;
    char text[512];
    s64 size = 0;
    result = nn::fs::GetFileSize(&size, file);
    if (!result.IsFailure()) {
        if (size < 0 || size > static_cast<s64>(sizeof(text))) size = 0;  // not a switch file: all off
        result = nn::fs::ReadFile(file, 0, text, static_cast<u64>(size));
    }
    nn::fs::CloseFile(file);
    if (result.IsFailure()) return Read::Retry;
    pure::parseSwitches({text, static_cast<std::size_t>(size)}, g_ids, g_count, g_saved);
    return Read::Present;
}

bool writeFile() {
    if (totk::engine::ensureSdCardMounted() == totk::engine::SdCardMountResult::Failed) return false;
    auto result = nn::fs::CreateDirectory(kDirectory);
    if (result.IsFailure() && result.GetInnerValueForDebug() != kPathAlreadyExists) return false;
    char text[512];
    const std::size_t size = pure::formatSwitches(g_ids, g_saved, g_count, text, sizeof(text));
    if (!size) return false;
    nn::fs::FileHandle file;
    result = nn::fs::OpenFile(&file, kFile, nn::fs::OpenMode_Write);
    if (result.IsFailure() && result.GetInnerValueForDebug() == kPathNotFound) {
        result = nn::fs::CreateFile(kFile, static_cast<s64>(size));
        if (!result.IsFailure()) result = nn::fs::OpenFile(&file, kFile, nn::fs::OpenMode_Write);
    }
    if (result.IsFailure()) return false;
    result = nn::fs::SetFileSize(file, static_cast<s64>(size));
    if (!result.IsFailure()) {
        const nn::fs::WriteOption flush{nn::fs::WriteOptionFlag_Flush};
        result = nn::fs::WriteFile(file, 0, text, static_cast<u64>(size), flush);
    }
    nn::fs::CloseFile(file);
    return !result.IsFailure();
}

bool wanted(std::size_t i) { return g_saved[i] && g_supported[i] && gates::fits(i); }
bool busy(std::size_t i) { return g_members[i].busy && g_members[i].busy(); }

pure::Pending pending(std::size_t i) {
    if (g_saved[i] && g_supported[i] && !gates::fits(i)) return pure::Pending::Unavailable;
    const bool want = wanted(i);
    if (want == (g_phase[i] == Phase::On)) return pure::Pending::None;
    if (g_phase[i] == Phase::Stopping) return pure::Pending::Now;  // switched back on while stopping
    if (!want && busy(i)) return pure::Pending::Busy;
    return pure::Pending::Now;
}

void routeHooks(std::size_t i, bool on) {
    gates::apply(i, on);
    if (i == kPhantom) climb::apply(on);
}

// The member is told first, while its hooks are still off; then they are routed to its code.
void turnOn(std::size_t i, const char* when) {
    if (g_members[i].enabled) g_members[i].enabled(true);
    if (g_phase[i] == Phase::Off) routeHooks(i, true);
    g_phase[i] = Phase::On;
    g_waitLogged[i] = false;
    if (when) Logging.Log("[fieldworks] SWITCH_APPLIED member=%s on=1 when=%s", g_ids[i], when);
}

void finishStop(std::size_t i) {
    routeHooks(i, false);
    g_phase[i] = Phase::Off;
    Logging.Log("[fieldworks] SWITCH_HOOKS_OFF member=%s", g_ids[i]);
}

// The member ends what it is doing and removes what it put in the world while its hooks still run its
// code; they go off kStopTicks later.
void beginStop(std::size_t i) {
    if (g_members[i].enabled) g_members[i].enabled(false);
    g_phase[i] = Phase::Stopping;
    g_stopTicks[i] = kStopTicks;
    g_waitLogged[i] = false;
    Logging.Log("[fieldworks] SWITCH_APPLIED member=%s on=0 when=now hooks_off_in=%u", g_ids[i], kStopTicks);
}

void start(bool fromFile) {
    g_read = true;
    for (std::size_t i = 0; i < g_count; ++i)
        if (wanted(i)) turnOn(i, nullptr);
    Logging.Log("[fieldworks] SWITCHES source=%s phantom=%u hookshot=%u fleet=%u columns=%u botw=%u bivouac=%u "
                "recall=%u survey=%u",
                fromFile ? "file" : "default-off", g_phase[0] == Phase::On, g_phase[1] == Phase::On,
                g_phase[2] == Phase::On, g_phase[3] == Phase::On, g_phase[4] == Phase::On, g_phase[5] == Phase::On,
                g_phase[6] == Phase::On, g_phase[7] == Phase::On);
}

void waitForMember(std::size_t i) {
    if (g_waitLogged[i]) return;
    g_waitLogged[i] = true;
    Logging.Log("[fieldworks] SWITCH_WAITING member=%s reason=busy", g_ids[i]);
}

// A changed switch applies on the next controller update.
void applyPending() {
    for (std::size_t i = 0; i < g_count; ++i) {
        if (!g_supported[i]) continue;
        if (g_phase[i] == Phase::Stopping && !wanted(i)) {
            if (--g_stopTicks[i] == 0) finishStop(i);
            continue;
        }
        switch (pending(i)) {
        case pure::Pending::Now:
            if (wanted(i)) turnOn(i, "now");
            else beginStop(i);
            break;
        case pure::Pending::Busy:
            waitForMember(i);
            break;
        default:
            break;
        }
    }
}

// --- wheel page ---------------------------------------------------------------------------------
bool abilitiesShown() { return g_phase[kBotw] != Phase::Off; }
const char* icon(std::size_t wedge) { return wedge < g_count && g_supported[wedge] ? g_icons[wedge] : nullptr; }
bool valid(std::size_t wedge) { return icon(wedge) != nullptr; }
bool checked(std::size_t wedge) { return valid(wedge) && g_saved[wedge]; }
const char16_t* caption(std::size_t wedge) {
    if (!valid(wedge)) return u"";
    std::size_t at =
        pure::switchCaption(g_members[wedge].caption, g_saved[wedge], pending(wedge), g_captions[wedge], 64);
    // Phantom Foothold changes terrain as it loads, so its switch shows only in areas loaded afterwards.
    if (wedge == kPhantom && pending(wedge) != pure::Pending::Unavailable) {
        for (const char16_t* text = u" (newly loaded areas)"; *text && at + 1 < 64; ++text)
            g_captions[wedge][at++] = *text;
        g_captions[wedge][at] = u'\0';
    }
    return g_captions[wedge];
}
void choose(std::size_t wedge) {
    if (!valid(wedge)) return;
    g_saved[wedge] = !g_saved[wedge];
    g_writeTries = 0;
    __atomic_store_n(&g_dirty, true, __ATOMIC_RELEASE);
    Logging.Log("[fieldworks] SWITCH_CHOSEN member=%s on=%u", g_ids[wedge], g_saved[wedge]);
}

constexpr botw_abilities::wheel::ExtraPage kPage{abilitiesShown, icon, valid, checked, caption, choose};

}  // namespace

void configure(const totk::suite::Member* members, const char* const* icons, const bool* supported,
               std::size_t count) {
    g_members = members;
    g_icons = icons;
    g_count = count < kMembers ? count : kMembers;
    for (std::size_t i = 0; i < g_count; ++i) {
        g_ids[i] = members[i].id;
        g_supported[i] = supported[i];
    }
}

void tick() {
    const unsigned now = ++g_ticks;
    if (!g_read) {
        if (now != 1 && now % kRetryTicks != 0) return;
        const Read read = readFile();
        ++g_readTries;
        if (read == Read::Present) {
            start(true);
        } else if (read == Read::Missing || g_readTries >= kTries) {
            if (read != Read::Missing)
                Logging.Log("[fieldworks] SWITCHES_UNREADABLE file=%s tries=%u", kFile, g_readTries);
            for (std::size_t i = 0; i < g_count; ++i) g_saved[i] = false;
            start(false);
        }
        return;
    }
    applyPending();
    if (__atomic_load_n(&g_dirty, __ATOMIC_ACQUIRE) && now % kRetryTicks == 0 && g_writeTries < kTries) {
        __atomic_store_n(&g_dirty, false, __ATOMIC_RELEASE);
        if (!writeFile()) {
            __atomic_store_n(&g_dirty, true, __ATOMIC_RELEASE);
            if (++g_writeTries == kTries) Logging.Log("[fieldworks] SWITCHES_WRITE_FAILED file=%s", kFile);
        }
    }
}

bool active(std::size_t member) { return member < g_count && g_phase[member] != Phase::Off; }

const botw_abilities::wheel::ExtraPage* wheelPage() { return &kPage; }

}  // namespace fieldworks::switches
