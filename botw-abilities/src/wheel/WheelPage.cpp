// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "WheelPage.hpp"
#include "WheelGame.hpp"
#include "WheelPages.hpp"
#include "totk/engine/Npad.hpp"
#include <lib.hpp>
#include <atomic>
#include <cstring>

namespace botw_abilities::wheel {
namespace {
namespace eng = totk::engine;
using eng::readMemory;
using eng::writeMemory;
using namespace game;

std::uintptr_t g_base{};
Choose g_stasis{}, g_cryonis{}, g_sphere{}, g_cube{}, g_wolf{};
Caption g_wolfCaption{};
Available g_wolfAvailable{};
unsigned g_offers{};
const ExtraPage *g_extra{};
std::atomic<bool> g_paging{};
std::atomic<unsigned> g_turns{};
eng::NpadReader g_npad;
PageInput g_input;

enum class Kind : std::uint8_t { Native, Abilities, Extra };
inline constexpr unsigned kMaxPages = 3;

// One wheel opening, owned by the UI thread.
struct Session {
    std::uintptr_t screen{};
    unsigned page{}, consumed{}, disabled{};
    unsigned pages{kPages};
    Kind kinds[kMaxPages]{Kind::Native, Kind::Abilities, Kind::Extra};
    std::int32_t captionCursor{-1};
    bool exiting{};
    Wedge native[kWedges]{};
    char paths[kWedges][kPathCapacity]{};
} g;

template <class Fn> Fn call(std::uintptr_t offset) { return reinterpret_cast<Fn>(g_base + offset); }

std::uintptr_t wedgeAt(std::size_t index) {
    const auto wedges = readMemory<std::uintptr_t>(g.screen + field::kWedges);
    return readMemory<std::uintptr_t>(wedges + index * sizeof(std::uintptr_t));
}
float frameOf(std::uintptr_t wedge, std::ptrdiff_t animator) {
    return readMemory<float>(readMemory<std::uintptr_t>(wedge + animator) + field::kAnimFrame);
}
std::int32_t idAt(std::int32_t slot) {
    return readMemory<std::int32_t>(g.screen + field::kIds + slot * sizeof(std::int32_t));
}

// Page 0 as the native open left it (including Autobuild and amiibo availability).
void snapshotNative() {
    for (std::size_t i = 0; i < kWedges; ++i) {
        const auto wedge = wedgeAt(i);
        const auto path = readMemory<const char *>(wedge + field::kIconPath);
        auto &out = g.native[i];
        out.path = nullptr;
        if (idAt(std::int32_t(i)) != 0 && path) {
            const auto length = strnlen(path, kPathCapacity);
            if (length < kPathCapacity) {
                std::memcpy(g.paths[i], path, length + 1);
                out.path = g.paths[i];
            } else {
                Logging.Log("[botw-abilities] WHEEL_NATIVE_ICON_SKIPPED wedge=%zu reason=path-too-long",
                            i);
            }
        }
        out.valid = frameOf(wedge, field::kInvalidAnim) == 0.0f;
        out.check = frameOf(wedge, field::kCheckAnim) != 0.0f;
    }
}

void showIcon(std::uintptr_t wedge, const char *path) {
    const auto pattern = readMemory<std::uintptr_t>(wedge + field::kIconPattern);
    const auto vtable = readMemory<std::uintptr_t>(pattern);
    readMemory<SetFrame>(vtable + field::kSetFrameSlot)(pattern, 0.0f);
    if (path) {
        call<RequestIcon>(offset::kRequestIcon)(wedge, &path, g.screen + field::kIconLoader);
        return;
    }
    // Unloading leaves a queued request in place, which would reload the old icon.
    const auto flags = readMemory<std::uint8_t>(wedge + field::kIconFlags);
    writeMemory<std::uint8_t>(wedge + field::kIconFlags, flags & ~3u);
    call<UnloadIcon>(offset::kUnloadIcon)(wedge);
}

Kind kind() { return g.kinds[g.page]; }
// plan() and exitPage() know two pages: 0 native, 1 abilities.
unsigned policyPage() { return kind() == Kind::Native ? 0 : 1; }

Wedge extraWedge(std::size_t i) {
    const char *path = g_extra->icon(i);
    return {path, path && g_extra->valid(i), path && g_extra->checked(i)};
}

void applyPage() {
    for (std::size_t i = 0; i < kWedges; ++i) {
        const auto wedge = wedgeAt(i);
        const auto shown = kind() == Kind::Extra ? extraWedge(i)
                                                 : plan(policyPage(), i, g.native[i], g_offers, g.disabled);
        showIcon(wedge, shown.path);
        call<SetFlag>(offset::kSetValid)(wedge, shown.valid);
        call<SetFlag>(offset::kSetCheck)(wedge, shown.check);
    }
}

void setCaption(const MessageString &text) {
    const auto layout = readMemory<std::uintptr_t>(g.screen + field::kLayout);
    call<LayoutSetMessage>(offset::kLayoutSetMessage)(layout, "T_Text_00", &text, true, nullptr);
}
MessageString plainText(const char16_t *text) { return {text, textLength(text), -1}; }

void nativeCaption(std::int32_t cursor) {
    const auto id = idAt(cursor);
    MessageString text{};
    bool found = true;
    if (const char *actor = abilityActor(id)) {
        found = call<PouchName>(offset::kPouchName)(&text, &actor, true);
    } else if (id == 1) {
        const char *file = "StaticMsg/Reference", *label = "MAP";
        found = call<FindMessage>(offset::kFindMessage)(&text, &file, &label, true);
    } else {
        text = plainText(u"");
    }
    if (!found) {
        Logging.Log("[botw-abilities] WHEEL_CAPTION_BLANK cursor=%d id=%d reason=name-not-found",
                    cursor, id);
        text = plainText(u"");
    }
    setCaption(text);
}

void tick(void *device) {
    const auto frame = g_npad.read(device);
    const auto &input = frame.snapshot();
    const auto step = g_input.step(input.buttons, input.freshSampleCount != 0,
                                   g_paging.load(std::memory_order_acquire));
    if (step.turn) {
        g_turns.fetch_add(1, std::memory_order_release);
        Logging.Log("[botw-abilities] WHEEL_PAGE_TAP");
    }
    if (step.mask)
        frame.maskOwnedButtons(step.mask);
}

void opened(std::uintptr_t screen) {
    g = Session{};
    const auto count = readMemory<std::int32_t>(screen + field::kWedgeCount);
    if (count != std::int32_t(kWedges)) {
        Logging.Log("[botw-abilities] WHEEL_PAGES_OFF reason=wedge-count wedges=%d expected=%zu",
                    count, kWedges);
        return;
    }
    g.screen = screen;
    if (g_extra) {
        g.pages = 0;
        g.kinds[g.pages++] = Kind::Native;
        if (!g_extra->abilitiesShown || g_extra->abilitiesShown())
            g.kinds[g.pages++] = Kind::Abilities;
        g.kinds[g.pages++] = Kind::Extra;
    }
    g.consumed = g_turns.load(std::memory_order_acquire);
    g_paging.store(true, std::memory_order_release);
}

void updated(std::uintptr_t screen) {
    if (!g.screen || screen != g.screen || g.exiting)
        return;
    const auto turns = g_turns.load(std::memory_order_acquire);
    const auto pending = turns - g.consumed;
    g.consumed = turns;
    const auto cursor = readMemory<std::int32_t>(screen + field::kCursor);
    const bool aimed = cursor >= 0 && cursor < std::int32_t(kWedges);
    const auto next = turnPage(g.page, pending, g.pages);
    const unsigned disabled = g_wolfAvailable && !g_wolfAvailable() ? offer(Choice::Wolf) : 0;
    if (disabled != g.disabled) {
        g.disabled = disabled;
        g.captionCursor = -1;
        if (kind() == Kind::Abilities && next == g.page) {
            const auto shown = plan(policyPage(), 2, g.native[2], g_offers, g.disabled);
            call<SetFlag>(offset::kSetValid)(wedgeAt(2), shown.valid);
        }
    }
    if (next != g.page) {
        if (kind() == Kind::Native)
            snapshotNative();
        g.page = next;
        applyPage();
        g.captionCursor = -1;
        if (kind() == Kind::Native) {
            if (aimed)
                nativeCaption(cursor);
        } else {
            // The native cursor idles on the equipped ability; clear it so the page opens with nothing aimed.
            writeMemory<std::int32_t>(screen + field::kCursor, -1);
            setCaption(plainText(u""));
        }
        Logging.Log("[botw-abilities] WHEEL_PAGE page=%u cursor=%d", g.page, cursor);
        return;
    }
    if (!aimed)
        return;
    // The native update re-captions on every cursor move; the other pages overwrite it.
    if (kind() == Kind::Extra && cursor != g.captionCursor) {
        const char16_t *caption = g_extra->icon(std::size_t(cursor)) ? g_extra->caption(std::size_t(cursor)) : u"";
        setCaption(plainText(caption ? caption : u""));
        g.captionCursor = cursor;
        return;
    }
    if (g.page != 0 && cursor != g.captionCursor) {
        const auto &entry = kAbilityPage[cursor];
        const bool shown = offered(entry, g_offers);
        const char16_t *caption = shown ? entry.caption : u"";
        if (shown && entry.choice == Choice::Wolf && g_wolfCaption)
            if (const auto note = g_wolfCaption()) caption = note;
        setCaption(plainText(caption));
        g.captionCursor = cursor;
    }
}

// True: close as cancelled.
bool exiting(std::uintptr_t screen, bool cancelled) {
    if (!g.screen || screen != g.screen)
        return false;
    g_paging.store(false, std::memory_order_release);
    g.exiting = true;
    const auto cursor = readMemory<std::int32_t>(screen + field::kCursor);
    if (kind() == Kind::Extra) {
        const bool pick = !cancelled && cursor >= 0 && cursor < std::int32_t(kWedges) &&
                          extraWedge(std::size_t(cursor)).valid;
        if (pick)
            g_extra->choose(std::size_t(cursor));
        Logging.Log("[botw-abilities] WHEEL_EXIT page=%u extra cursor=%d cancelled=%u picked=%u", g.page,
                    cursor, unsigned(cancelled), unsigned(pick));
        return true;
    }
    const unsigned disabled = g_wolfAvailable && !g_wolfAvailable() ? offer(Choice::Wolf) : 0;
    const auto exit = exitPage(policyPage(), cursor, cancelled, g_offers, disabled);
    bool chosen = false;
    if (exit.choice == Choice::Stasis)
        chosen = g_stasis(screen);
    else if (exit.choice == Choice::Cryonis)
        chosen = g_cryonis(screen);
    else if (exit.choice == Choice::Sphere)
        chosen = g_sphere(screen);
    else if (exit.choice == Choice::Cube)
        chosen = g_cube(screen);
    else if (exit.choice == Choice::Wolf)
        chosen = g_wolf(screen);
    Logging.Log("[botw-abilities] WHEEL_EXIT page=%u cursor=%d cancelled=%u choice=%u chosen=%u",
                g.page, cursor, unsigned(cancelled), unsigned(exit.choice), unsigned(chosen));
    return exit.cancel;
}

void closed(std::uintptr_t screen) {
    if (!g.screen || screen != g.screen)
        return;
    g_paging.store(false, std::memory_order_release);
    g = Session{};
}

HOOK_DEFINE_TRAMPOLINE(WheelPageNpadHook) {
    static void Callback(void *device) {
        Orig(device);
        tick(device);
    }
};
HOOK_DEFINE_TRAMPOLINE(WheelPageOpenHook) {
    static float Callback(std::uintptr_t screen) {
        const auto result = Orig(screen);
        opened(screen);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(WheelPageUpdateHook) {
    static void Callback(std::uintptr_t screen) {
        Orig(screen);
        updated(screen);
    }
};
HOOK_DEFINE_TRAMPOLINE(WheelPageExitHook) {
    static std::uintptr_t Callback(std::uintptr_t screen, unsigned char cancel) {
        // The abilities see the pick and the forced cancel in their own exit hooks.
        if (exiting(screen, (cancel & 1) != 0))
            cancel = 1;
        return Orig(screen, cancel);
    }
};
HOOK_DEFINE_TRAMPOLINE(WheelPageCloseHook) {
    static std::uintptr_t Callback(std::uintptr_t screen) {
        const auto result = Orig(screen);
        closed(screen);
        return result;
    }
};
} // namespace

void setExtraPage(const ExtraPage *page) { g_extra = page; }

bool install(std::uintptr_t base, Choose stasis, Choose cryonis, Choose sphere, Choose cube, Choose wolf,
             Caption wolfCaption, Available wolfAvailable) {
    if (!totk::port::codeMatches(base, kSites, "botw-abilities", "WHEEL_PAGE_REFUSED"))
        return false;
    g_base = base;
    g_stasis = stasis;
    g_cryonis = cryonis;
    g_sphere = sphere;
    g_cube = cube;
    g_wolf = wolf;
    g_wolfCaption = wolfCaption;
    g_wolfAvailable = wolfAvailable;
    g_offers = (stasis ? offer(Choice::Stasis) : 0u) | (cryonis ? offer(Choice::Cryonis) : 0u) |
               (sphere ? offer(Choice::Sphere) : 0u) | (cube ? offer(Choice::Cube) : 0u) |
               (wolf ? offer(Choice::Wolf) : 0u);
    WheelPageNpadHook::InstallAtOffset(offset::kNpadCalc);
    WheelPageOpenHook::InstallAtOffset(offset::kWheelOpen);
    WheelPageUpdateHook::InstallAtOffset(offset::kWheelUpdate);
    WheelPageExitHook::InstallAtOffset(offset::kWheelExit);
    WheelPageCloseHook::InstallAtOffset(offset::kWheelClose);
    return true;
}
} // namespace botw_abilities::wheel
