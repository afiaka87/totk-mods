// SPDX-License-Identifier: MIT
#include "SurveyStartup.hpp"

#include <lib.hpp>

#if SURVEY_STARTUP_DIAGNOSTIC
#include "SurveyStartupDiagnostic.hpp"
#endif

namespace zonai_survey::engine {
namespace {

constexpr std::uintptr_t kOpenFileCall = 0x02A26EF8;

struct HookFingerprint {
    std::uintptr_t offset;
    std::uint32_t words[2];
};

constexpr HookFingerprint kHookFingerprints[] = {
    {0x007F61D0, {0xAA1F03F9, 0x92401D1B}},
    {0x00A9123C, {0x940005CA, 0xB001E4A8}},
    {0x00818340, {0x6A28013F, 0x54001160}},
    {0x0081911C, {0xFC1B0FEA, 0x6D00A3E9}},
    {0x02AEFE74, {0xD10143FF, 0xA9027BFD}},
    {0x00858590, {0xD10743FF, 0xA9187BFD}},
    {0x02A267BC, {0xD102C3FF, 0xA9057BFD}},
    {kOpenFileCall, {0x9403C27A, 0xB9005E60}},
#if SURVEY_STARTUP_DIAGNOSTIC
    {0x00A9177C, {0x913203FF, 0xA9454FF4}},
    {0x00C12070, {0xA9BD7BFD, 0xF9000BF5}},
    {0x00880BD8, {0xA9BB7BFD, 0xF9000BF9}},
    {0x00B6E8E4, {0xD101C3FF, 0xA9037BFD}},
    {0x007F67EC, {0xD101C3FF, 0xA9017BFD}},
    {0x007FA8F0, {0xA9BD7BFD, 0xF9000BF5}},
    {0x0074C140, {0xAA0003E8, 0xAA0103E0}},
    {0x00754E30, {0xA9BF7BFD, 0x910003FD}},
    {0x02B18480, {0xB000D7F0, 0xF947DE11}},
    {0x007F6614, {0x910363E0, 0x97FDE95D}},
    {0x007F4CC8, {0xD10283FF, 0xA9047BFD}},
    {0x00817F90, {0xA9BA7BFD, 0xA9016FFC}},
#endif
};





}

bool startupImageSupported(std::uintptr_t mainBase) {
    const auto& text = exl::util::GetMainModuleInfo().m_Text;
    for (const auto& site : kHookFingerprints) {
        if (mainBase + site.offset < text.m_Start ||
            mainBase + site.offset + sizeof(site.words) > text.GetEnd()) {
            Logging.Log("[survey-startup] hook outside text offset=%lx", site.offset);
            return false;
        }
        const auto* words = reinterpret_cast<const std::uint32_t*>(mainBase + site.offset);
        for (unsigned i = 0; i < 2; ++i) {
            if (words[i] != site.words[i]) {
                Logging.Log("[survey-startup] unsupported hook offset=%lx actual=%08x expected=%08x",
                            site.offset + i * 4, words[i], site.words[i]);
                return false;
            }
        }
    }
    return true;
}



}
