#include "RecallGameClock.hpp"

#include <cstring>
#include <lib.hpp>

#include "RecallFrameTicket.hpp"
#include "RecallPoseSession.hpp"

namespace self_recall::game_clock {
namespace {

constexpr std::uintptr_t kPhysicsUpdateDeltaFrame = 0x007EDC00;
constexpr std::uintptr_t kPhysicsSystemIndirect = 0x0462E038;
std::uintptr_t g_mainBase = 0;
pure::GameTime g_time;
pure::FrameMailbox<pure::GameTimeSnapshot> g_published;
std::uint64_t g_faults = 0;

template <class T>
T read(const void* base, std::size_t offset = 0) {
    T value;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

HOOK_DEFINE_TRAMPOLINE(PhysicsFrameTimeHook) {
    static void Callback(void* module, const void* pauseContext, float frameScale) {
        Orig(module, pauseContext, frameScale);
        const auto* holder = read<const void*>(reinterpret_cast<const void*>(
            g_mainBase + kPhysicsSystemIndirect));
        const auto* system = holder ? read<const void*>(holder) : nullptr;
        const auto* timeState = system ? read<const void*>(system, 0xC8) : nullptr;
        const bool paused = timeState && (read<std::uint32_t>(timeState, 0x18) & 1u);
        const auto value = g_time.update(frameScale, paused, timeState != nullptr);
        pose_session::latchPresentation(value);
        const bool published = g_published.publish(value);
        if (!published || value.status == pure::GameTimeStatus::Unavailable ||
            value.status == pure::GameTimeStatus::InvalidDelta ||
            value.status == pure::GameTimeStatus::Exhausted) {
            if (++g_faults <= 4 || (g_faults % 300) == 0)
                Logging.Log("[self-recall] game clock unavailable: status=%u published=%u scale=%f total=%llu",
                            static_cast<unsigned>(value.status), static_cast<unsigned>(published),
                            static_cast<double>(frameScale), static_cast<unsigned long long>(g_faults));
        }
    }
};

}  // namespace

void install(std::uintptr_t mainBase) {
    g_mainBase = mainBase;
    PhysicsFrameTimeHook::InstallAtOffset(kPhysicsUpdateDeltaFrame);
}

bool snapshot(pure::GameTimeSnapshot& out) { return g_published.snapshot(out); }

}  // namespace self_recall::game_clock
