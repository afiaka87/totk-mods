// Copyright (c) HanSoloBlaster, afiaka87
// Zonai Fieldworks: eight mods in one subsdk6 module, each switched on or off per the player's saved choice.
#include <lib.hpp>

#include "arrowbound/GameProfiles.hpp"
#include "fieldworks/Gates.hpp"
#include "fieldworks/Switches.hpp"
#include "totk/port/Generated.hpp"
#include "totk/suite/Member.hpp"
#include "wheel/WheelPage.hpp"

namespace phantom::integration { bool install(std::uintptr_t mainBase); }
namespace zonai_hookshot::suite {
bool install(std::uintptr_t mainBase);
void enabled(bool on);
}  // namespace zonai_hookshot::suite
namespace linked_stick::suite {
bool install(std::uintptr_t mainBase);
void enabled(bool on);
bool busy();
}  // namespace linked_stick::suite
namespace material_columns::suite {
bool install(std::uintptr_t mainBase);
void enabled(bool on);
}  // namespace material_columns::suite
namespace botw_abilities::suite {
bool install(std::uintptr_t mainBase);
void enabled(bool on);
}  // namespace botw_abilities::suite
namespace bivouac::suite {
bool install(std::uintptr_t mainBase);
void enabled(bool on);
bool busy();
}  // namespace bivouac::suite
namespace self_recall::suite {
bool install(std::uintptr_t mainBase);
void enabled(bool on);
}  // namespace self_recall::suite
namespace zonai_survey::suite {
bool install(std::uintptr_t mainBase);
void enabled(bool on);
}  // namespace zonai_survey::suite

namespace {

using arrowbound::profiles::Game;
using arrowbound::profiles::GameVersion;

// Standalone slot order, so hooks on a shared routine chain as the separate modules did.
constexpr totk::suite::Member kMembers[] = {
    {"phantom-foothold", u"Phantom Foothold", &phantom::integration::install, nullptr, nullptr},
    {"zonai-hookshot", u"Zonai Hookshot", &zonai_hookshot::suite::install, &zonai_hookshot::suite::enabled,
     nullptr},
    {"fleet-sync", u"Fleet Sync", &linked_stick::suite::install, &linked_stick::suite::enabled,
     &linked_stick::suite::busy},
    {"material-columns", u"Material Columns", &material_columns::suite::install, &material_columns::suite::enabled, nullptr},
    {"botw-abilities", u"BotW Abilities", &botw_abilities::suite::install, &botw_abilities::suite::enabled, nullptr},
    {"bivouac", u"Bivouac", &bivouac::suite::install, &bivouac::suite::enabled, &bivouac::suite::busy},
    {"self-recall", u"Self Recall", &self_recall::suite::install, &self_recall::suite::enabled, nullptr},
    {"zonai-survey", u"Zonai Survey", &zonai_survey::suite::install, &zonai_survey::suite::enabled, nullptr},
};
static_assert(sizeof(kMembers) / sizeof(kMembers[0]) == fieldworks::gates::kMembers);
constexpr std::size_t kBotw = 4;

constexpr const char* kIcons[] = {
    "UI/Tex/Icon/FwPhantom.bntx", "UI/Tex/Icon/FwHookshot.bntx", "UI/Tex/Icon/FwFleetSync.bntx",
    "UI/Tex/Icon/FwColumns.bntx", "UI/Tex/Icon/FwAbilities.bntx", "UI/Tex/Icon/FwBivouac.bntx",
    "UI/Tex/Icon/FwSelfRecall.bntx", "UI/Tex/Icon/FwSurvey.bntx",
};

const Game* g_game = nullptr;
bool g_supported[fieldworks::gates::kMembers]{};

// The process memory left when the game starts creating its heaps; a startup hang from a failed game
// allocation is read against these numbers.
void logMemory() {
    u64 total = 0, used = 0, systemTotal = 0, systemUsed = 0;
    const Result r0 = svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    const Result r1 = svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    const Result r2 = svcGetInfo(&systemTotal, InfoType_SystemResourceSizeTotal, CUR_PROCESS_HANDLE, 0);
    const Result r3 = svcGetInfo(&systemUsed, InfoType_SystemResourceSizeUsed, CUR_PROCESS_HANDLE, 0);
    Logging.Log("[fieldworks] MEMORY total=%lu used=%lu free=%lu system=%lu/%lu results=%x,%x,%x,%x", total,
                used, total - used, systemUsed, systemTotal, r0, r1, r2, r3);
}

// Outermost controller hook: the switches are read before any member's controller code runs.
HOOK_DEFINE_TRAMPOLINE(SwitchesNpadHook) {
    static void Callback(void* device) {
        fieldworks::switches::tick();
        Orig(device);
    }
};

}  // namespace

bool totk::suite::gameIs121() { return g_game && g_game->version == GameVersion::V121; }

extern "C" void exl_main(void* /*x0*/, void* /*x1*/) {
    exl::hook::Initialize();
    const std::uintptr_t base = exl::util::modules::GetTargetStart();
    const std::size_t textSize = exl::util::GetMainModuleInfo().m_Text.m_Size;
    g_game = arrowbound::profiles::select(textSize, [base](std::ptrdiff_t offset) {
        return *reinterpret_cast<const std::uint32_t*>(base + offset);
    });
    if (!g_game) {
        Logging.Log("[fieldworks] unknown game build main_text=%zx; nothing installed", textSize);
        return;
    }
    Logging.Log("[fieldworks] v0.9.4 TotK %s module_offset=%lx", g_game->name,
                exl::util::modules::GetSelfStart() - base);
    // Fleet Sync, Bivouac and BotW Abilities name game code by its 1.2.1 offset (totk/port/Port.hpp);
    // they install on 1.2.1 and 1.4.3 and refuse the other versions.
    const auto ported = g_game->version == GameVersion::V121   ? totk::port::Game::V121
                        : g_game->version == GameVersion::V143 ? totk::port::Game::V143
                                                               : totk::port::Game::Unsupported;
    if (!totk::port::use(ported, totk::port::generated::table(), base)) {
        Logging.Log("[fieldworks] PORTED members off: game=%s early_addresses=%u", g_game->name,
                    totk::port::state.early);
    }

    for (std::size_t i = 0; i < fieldworks::gates::kMembers; ++i) {
        fieldworks::gates::beginMember(i);
        g_supported[i] = kMembers[i].install(base);
        fieldworks::gates::endMember();
        const auto counts = fieldworks::gates::counts(i);
        Logging.Log("[fieldworks] MEMBER %s supported=%u gated=%u always_on=%u fits=%u", kMembers[i].id,
                    g_supported[i], counts.gated, counts.alwaysOn, fieldworks::gates::fits(i));
    }
    // Last, so that a climbing site another hook already changed refuses the edits.
    if (g_supported[0]) fieldworks::climb::install(base, g_game->name);

    fieldworks::switches::configure(kMembers, kIcons, g_supported, fieldworks::gates::kMembers);
    if (g_supported[kBotw]) botw_abilities::wheel::setExtraPage(fieldworks::switches::wheelPage());
    SwitchesNpadHook::InstallAtOffset(g_game->hooks.npadCalc.offset);
    const auto forms = fieldworks::gates::hookForms();
    Logging.Log("[fieldworks] HOOKS near=%u far=%u recorded_sites=%u", forms.nearHooks, forms.farHooks,
                forms.recordedSites);
    logMemory();
    Logging.Log("[fieldworks] READY gate_stubs=%u wheel=%s", fieldworks::gates::stubsUsed(),
                g_supported[kBotw] ? "abilities-wheel" : "none");
}

extern "C" NORETURN void exl_exception_entry() { EXL_ABORT("unreachable"); }
