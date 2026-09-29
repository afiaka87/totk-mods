// SPDX-License-Identifier: MIT
#include <doctest.h>
#include <string>
#include "../../src/pure/SurveyOptions.hpp"
#include "../../src/presentation/ScanPresentation.hpp"
using namespace zonai_survey::pure;

TEST_CASE("Release profiles use the selected range and cooldown") {
#if SURVEY_CONSTRAINED
    CHECK(zonai_survey::options::nextRange() == 180.f);
    CHECK(zonai_survey::options::cooldownSeconds() == 7);
#else
    CHECK(zonai_survey::options::nextRange() == kMaxRange);
    CHECK(zonai_survey::options::cooldownSeconds() == 3);
#endif
    CHECK(std::string(zonai_survey::presentation::displayText(ScanVerdict::CoolingDown)) == "survey is recharging");
}

TEST_CASE("Cooldown is ready at the boundary and refusals cannot extend it") {
    constexpr auto second=kSystemTicksPerSecond;
    for (unsigned seconds : {kRegularCooldown, kConstrainedCooldown}) {
        SurveyCooldown cooldown;
        CHECK(cooldown.remaining(0) == 0);
        cooldown.start(100,seconds);
        CHECK(cooldown.remaining(100) == seconds*second);
        if (seconds) {
            for (unsigned i=0; i<100; ++i) CHECK(cooldown.remaining(100) == seconds*second);
            CHECK(cooldown.remaining(100+seconds*second-1) == 1);
        }
        CHECK(cooldown.remaining(100+seconds*second) == 0);
        CHECK(cooldown.remaining(100+(seconds+100)*second) == 0);
        CHECK(cooldown.remaining(99) == 0);
    }
}

TEST_CASE("Survey distance is horizontal radial range, including the sides of the cone") {
    for (float range : {kConstrainedRange, kMaxRange}) {
        CHECK(withinSurveyRange(range,0,range));
        CHECK(withinSurveyRange(0,-range,range));
        CHECK_FALSE(withinSurveyRange(range+0.01f,0,range));
        CHECK_FALSE(withinSurveyRange(range,range,range));
        CHECK(withinSurveyRange(range*0.6f,range*0.7f,range));
    }
}
