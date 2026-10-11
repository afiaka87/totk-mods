// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombSignals.hpp"
#include "CodeGuard.hpp"
#include "BombInput.hpp"
#include "NativePlayerKeys.hpp"
#include "NativeAbilityAdmission.hpp"
#include "totk/engine/Pointer.hpp"
#include <lib.hpp>
#include <cmath>

namespace remote_bomb::engine {
namespace eng = totk::engine;
namespace {
constexpr CodeGuard guards[]{
    {0x7EDC00, 0xF9400828, 0xB40002A8}, {0x2A267BC, 0xD102C3FF, 0xA9057BFD},
    {0x1B1DC94, 0xD10283FF, 0xFD001BE8}, {0x1B1E65C, 0xD10203FF, 0x6D032BEB},
    {0x19AD3F4, 0xA9BE7BFD, 0xA9014FF4}, {0x1B1EEF8, 0xD10103FF, 0xA9017BFD},
    {0x1B1EEB8, 0xA9BE7BFD, 0xA9014FF4}, {0x1D30CBC, 0xD10143FF, 0xA9027BFD},
    {0x1D29AAC, 0xA9BE7BFD, 0xF9000BF3},
    {0x932610, 0xD10143FF, 0xA9017BFD},
    {0xCE0EEC, 0xA9BA7BFD, 0xA9016FFC}, {0x1706D80, 0xA9BE7BFD, 0xF9000BF3},
    {0xD4499C, 0x51002028, 0x71000D1F}, {0xD44A08, 0xA9BE7BFD, 0xF9000BF3},
    {0x942FE8, 0x340002A3, 0xF9400049}, {0x935BE0, 0xA9BB7BFD, 0xA90167FA},
    {0x92FF20, 0xA9BC7BFD, 0xF9000BF7},
    {0xD44600, 0xA9BD7BFD, 0xA90157F6},
    {0x1A7450C, 0xD10403FF, 0xA90A7BFD}, {0x1A733D0, 0xD10103FF, 0xA9027BFD},
    {0xBC7610, 0xF9400408, 0xF9400508},
    {0x2B17270, 0xD000D7F0, 0xF9435A11}, {0x2B17280, 0xD000D7F0, 0xF9435E11},
};
bool mapSelected(std::uintptr_t screen) {
    const auto count = eng::readMemory<int>(screen + 0x1A8);
    const auto cursor = eng::readMemory<int>(screen + 0x23C);
    return count >= 1 && count <= 8 && cursor >= 0 && cursor < count &&
           !eng::readMemory<unsigned char>(screen + 0x244) &&
           eng::readMemory<unsigned char>(screen + 0x245) &&
           eng::readMemory<unsigned>(screen + 0x270 + 4 * cursor) == 1;
}
template <class Change>
void changeWheel(Signals &signals, std::uintptr_t screen, Change change) {
    if (signals.wheelOwner.load() != screen)
        return;
    auto prior = signals.wheel.load();
    auto wheel = input::unpack(prior);
    change(wheel);
    signals.wheel.compare_exchange_strong(prior, input::pack(wheel));
}
}
bool validateRuntime(std::uintptr_t base) {
    if (!base) {
        Logging.Log("[remote-bomb] BUILD_REFUSED base=0");
        return false;
    }
    return codeMatches(base, guards, "BUILD_REFUSED");
}
void publishPhysicsTime(std::uintptr_t base, Signals &signals, float frames) {
    const auto system = eng::readMemory<std::uintptr_t>(base + 0x471DCB0);
    const auto time = eng::isPlausibleAddress(system) ? eng::readMemory<std::uintptr_t>(system + 0xC8) : 0;
    const bool running = eng::isPlausibleAddress(time) &&
                         !(eng::readMemory<unsigned>(time + 0x18) & 1) &&
                         std::isfinite(frames) && frames > 0 && frames < 100;
    if (running)
        signals.clock.fetch_add(std::uint64_t(double(frames) * 4294967296.0 + .5),
                                std::memory_order_release);
    signals.running.store(running, std::memory_order_release);
}
void wheelOpened(Signals &signals, std::uintptr_t screen) {
    auto epoch = signals.epoch.fetch_add(1) + 1;
    if (!epoch)
        epoch = signals.epoch.fetch_add(1) + 1;
    signals.wheelOwner.store(screen);
    signals.wheel.store(input::pack({epoch, true, mapSelected(screen)}));
}
void wheelUpdated(Signals &signals, std::uintptr_t screen) {
    changeWheel(signals, screen, [&](input::Wheel &wheel) {
        if (wheel.open && !wheel.exited)
            wheel.map = mapSelected(screen);
    });
}
bool wheelCancelRequested(const Signals &signals, std::uintptr_t screen) {
    const auto wheel = input::unpack(signals.wheel.load());
    return signals.wheelOwner.load() == screen && wheel.open && wheel.epoch &&
           signals.request.load() == wheel.epoch;
}
void wheelExited(Signals &signals, std::uintptr_t screen, bool cancelled) {
    changeWheel(signals, screen, [&](input::Wheel &wheel) {
        wheel.open = wheel.map = false;
        wheel.exited = true;
        wheel.cancelled = cancelled;
    });
}
void wheelClosed(Signals &signals, std::uintptr_t screen) {
    changeWheel(signals, screen, [](input::Wheel &wheel) {
        wheel.open = wheel.map = false;
        wheel.closed = true;
    });
}
void nativeSelectionAccepted(Signals &signals, std::uintptr_t screen) {
    const auto count = eng::readMemory<int>(screen + 0x1A8);
    const auto cursor = eng::readMemory<int>(screen + 0x23C);
    if (count < 1 || count > 8 || cursor < 0 || cursor >= count) {
        Logging.Log("[remote-bomb] SELECTION_REFUSED count=%d cursor=%d", count, cursor);
        return;
    }
    const auto selected = eng::readMemory<unsigned>(screen + 0x270 + 4 * cursor);
    signals.nativeSelections.fetch_add(1, std::memory_order_release);
    signals.remembered.store(false, std::memory_order_release);
    Logging.Log("[remote-bomb] NATIVE_SELECTION id=%u", selected);
}
void maskRecentAbilityKey(std::uintptr_t base, const Signals &signals, std::uintptr_t node) {
    if (!signals.remembered.load(std::memory_order_acquire) ||
        !signals.running.load(std::memory_order_acquire))
        return;
    const auto actor = reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(base + 0xBC7610)(node);
    if (actor != signals.player.load(std::memory_order_acquire))
        return;
    const auto result = filterAbilityKeys(node, true,
        [](std::uintptr_t address) { return eng::readMemory<std::uint64_t>(address); },
        [](std::uintptr_t address, std::uint64_t value) { eng::writeMemory(address, value); },
        [](std::uintptr_t address) { return eng::isPlausibleAddress(address); });
    static unsigned failures{}, reports{};
    if (result.invalid && failures++ < 12)
        Logging.Log("[remote-bomb] KEY_REFUSED node=%p outputs=%u", reinterpret_cast<void *>(node),
                    result.invalid);
    if (result.changed && reports++ < 12)
        Logging.Log("[remote-bomb] NATIVE_ABILITY_MASKED outputs=%u", result.changed);
}
std::uint64_t nativeAbilityAdmission(std::uint64_t (*original)(std::uintptr_t),
                                    const Signals &signals, std::uintptr_t actor) {
    return recentAbilityAdmission(actor, signals.remembered.load(std::memory_order_acquire),
        signals.player.load(std::memory_order_acquire), original, [&signals](std::uintptr_t player) {
            // One line per run of refusals: the first after a third of a second without one.
            static std::atomic<std::uint64_t> last{};
            static std::atomic<unsigned> reports{};
            const auto now = signals.clock.load(std::memory_order_acquire);
            const auto previous = last.exchange(now, std::memory_order_relaxed);
            if ((!previous || now - previous >= (std::uint64_t(10) << 32)) &&
                reports.fetch_add(1, std::memory_order_relaxed) < 200)
                Logging.Log("[remote-bomb] NATIVE_ABILITY_SUPPRESSED player=%p",
                            reinterpret_cast<void *>(player));
        });
}
void cancelOwnedAmiibo(std::uintptr_t base, const Signals &signals, std::uintptr_t node) {
    if (!signals.remembered.load(std::memory_order_acquire) || !eng::isPlausibleAddress(node))
        return;
    const auto actor = reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(base + 0xBC7610)(node);
    const auto state = eng::readMemory<unsigned>(node + 32);
    if (!input::cancelNativeAmiibo(true, signals.running.load(std::memory_order_acquire),
                                   actor == signals.player.load(std::memory_order_acquire), state))
        return;
    const auto prior = eng::readMemory<unsigned>(node + 24);
    eng::writeMemory(node + 24, input::nativeCancelFlags(prior));
    static unsigned reports{};
    if (reports++ < 12)
        Logging.Log("[remote-bomb] AMIIBO_AIM_CANCEL state=%u flags=%08x", state, prior);
}
}
