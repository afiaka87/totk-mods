#include <lib.hpp>

#include "RecallAnimation.hpp"

#define SRALOG(...) Logging.Log("[self-recall] " __VA_ARGS__)

namespace self_recall::anim {
namespace {

namespace off {
constexpr ptrdiff_t GetASController = 0x00FCA854;
constexpr ptrdiff_t GetCommandName = 0x00D2881C;
constexpr ptrdiff_t GetAnimRate = 0x00BE3F64;
constexpr ptrdiff_t GetCurrentFrame = 0x01345E1C;
}  // namespace off

constexpr ptrdiff_t kCtrlSlotCount = 0x18;
constexpr int kMaxSlots = 32;

constexpr int kUsurperTableSize = 4;
constexpr int kUsurperNameCap = 32;

struct Stats {
    std::uint32_t drives = 0;
    std::uint32_t none = 0;
    std::uint32_t badSlot = 0;
    std::uint32_t commandMatch = 0;
    std::uint32_t commandMismatch = 0;
    std::uint32_t mismatchStreak = 0;
    std::uint32_t longestStreak = 0;
    std::uint32_t firstMismatchDrive = 0;
    std::uint32_t usurperOverflow = 0;
    char usurperNames[kUsurperTableSize][kUsurperNameCap + 1]{};
    std::uint32_t usurperCounts[kUsurperTableSize]{};
};

std::uintptr_t g_mainBase = 0;
Stats g_stats{};

bool okPtr(std::uintptr_t pointer) {
    return pointer >= 0x1000 && (pointer & 7) == 0 &&
           pointer < (1ull << 40);
}

bool sameName(const char* a, const char* b) {
    if (!a || !b) return false;
    for (int i = 0; i < 32; ++i) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return false;
}

std::uint8_t kindForCommand(const char* name) {
    for (std::uint8_t kind = kKindMove;
         kind < self_recall::pure::kKindCount; ++kind) {
        if (sameName(name, pure::allowlistedCommandName(kind))) return kind;
    }
    return kKindNone;
}

std::uintptr_t resolveController(void* playerActor) {
    if (!g_mainBase || !playerActor) return 0;
    const auto getController =
        reinterpret_cast<std::uintptr_t (*)(void*)>(
            g_mainBase + off::GetASController);
    const std::uintptr_t controller = getController(playerActor);
    return okPtr(controller) ? controller : 0;
}

int slotCount(std::uintptr_t controller) {
    const std::int32_t count =
        *reinterpret_cast<const std::int32_t*>(controller + kCtrlSlotCount);
    if (count < 0) return 0;
    return count > kMaxSlots ? kMaxSlots : count;
}

const char* commandName(std::uintptr_t controller, int slot) {
    const auto getName =
        reinterpret_cast<const char* (*)(void*, std::uint32_t)>(
            g_mainBase + off::GetCommandName);
    return getName(reinterpret_cast<void*>(controller),
                   static_cast<std::uint32_t>(slot));
}

float animRate(std::uintptr_t controller, int slot) {
    const auto getRate = reinterpret_cast<float (*)(void*, std::uint32_t)>(
        g_mainBase + off::GetAnimRate);
    return getRate(reinterpret_cast<void*>(controller),
                   static_cast<std::uint32_t>(slot));
}

float currentFrame(std::uintptr_t controller, int slot) {
    const auto getFrame = reinterpret_cast<float (*)(void*, std::uint32_t)>(
        g_mainBase + off::GetCurrentFrame);
    return getFrame(reinterpret_cast<void*>(controller),
                    static_cast<std::uint32_t>(slot));
}

void recordUsurper(const char* live) {
    const char* name = live ? live : "(null)";
    for (int i = 0; i < kUsurperTableSize; ++i) {
        if (g_stats.usurperCounts[i] == 0) {
            int j = 0;
            for (; j < kUsurperNameCap && name[j]; ++j)
                g_stats.usurperNames[i][j] = name[j];
            g_stats.usurperNames[i][j] = 0;
            g_stats.usurperCounts[i] = 1;
            return;
        }
        if (sameName(g_stats.usurperNames[i], name)) {
            ++g_stats.usurperCounts[i];
            return;
        }
    }
    ++g_stats.usurperOverflow;
}

void logSummary(const char* reason) {
    SRALOG(
        "ANIM_FORWARD_SUMMARY %s drives=%u none=%u badslot=%u match=%u "
        "mismatch=%u restarts=0",
        reason ? reason : "stop", g_stats.drives, g_stats.none,
        g_stats.badSlot, g_stats.commandMatch, g_stats.commandMismatch);
    if (g_stats.commandMismatch == 0) return;
    SRALOG(
        "ANIM_USURPERS streak=%u first_drive=%u overflow=%u "
        "%s:%u %s:%u %s:%u %s:%u",
        g_stats.longestStreak, g_stats.firstMismatchDrive,
        g_stats.usurperOverflow,
        g_stats.usurperCounts[0] ? g_stats.usurperNames[0] : "-",
        g_stats.usurperCounts[0],
        g_stats.usurperCounts[1] ? g_stats.usurperNames[1] : "-",
        g_stats.usurperCounts[1],
        g_stats.usurperCounts[2] ? g_stats.usurperNames[2] : "-",
        g_stats.usurperCounts[2],
        g_stats.usurperCounts[3] ? g_stats.usurperNames[3] : "-",
        g_stats.usurperCounts[3]);
}

}  // namespace

void initialize(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    g_stats = {};
}

Sample capture(void* playerActor) {
    Sample sample{0.0f, 0.0f, kKindNone, kInvalidSlot};
    const std::uintptr_t controller = resolveController(playerActor);
    if (!controller) return sample;
    const int count = slotCount(controller);
    for (int slot = 0; slot < count; ++slot) {
        const std::uint8_t kind = kindForCommand(commandName(controller, slot));
        if (kind == kKindNone) continue;
        const float frame = currentFrame(controller, slot);
        const float rate = animRate(controller, slot);
        if (!__builtin_isfinite(frame) || !__builtin_isfinite(rate))
            return sample;
        sample.frame = frame;
        sample.rate = rate;
        sample.kind = kind;
        sample.slot = static_cast<std::uint8_t>(slot);
        return sample;
    }
    return sample;
}

void beginRewind(void* playerActor) {
    (void)playerActor;
    g_stats = {};
    SRALOG("ANIM_BEGIN live carrier for historical render playback");
}

void drive(void* playerActor, std::uint8_t sampleKind,
           std::uint8_t sampleSlot) {
    ++g_stats.drives;
    if (sampleKind == kKindNone) {
        ++g_stats.none;
        return;
    }
    const char* desired = pure::allowlistedCommandName(sampleKind);
    const std::uintptr_t controller = resolveController(playerActor);
    const int slot = static_cast<int>(sampleSlot);
    if (!desired || !controller || slot < 0 || slot >= slotCount(controller)) {
        ++g_stats.badSlot;
        return;
    }

    const char* live = commandName(controller, slot);
    if (sameName(live, desired)) {
        ++g_stats.commandMatch;
        g_stats.mismatchStreak = 0;
        return;
    }
    ++g_stats.commandMismatch;
    if (g_stats.firstMismatchDrive == 0)
        g_stats.firstMismatchDrive = g_stats.drives;
    if (++g_stats.mismatchStreak > g_stats.longestStreak)
        g_stats.longestStreak = g_stats.mismatchStreak;
    recordUsurper(live);
}

void release(void* playerActor, const char* reason) {
    (void)playerActor;
    if (g_stats.drives != 0) logSummary(reason);
    g_stats = {};
}

void abandon() {
    if (g_stats.drives != 0) logSummary("abandon");
    g_stats = {};
}

}  // namespace self_recall::anim
