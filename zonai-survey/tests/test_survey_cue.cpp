// SPDX-License-Identifier: MIT

#include <doctest.h>

#include <cstring>

#include "SurveyCue.hpp"

using namespace zonai_survey::pure;

TEST_CASE("the survey's start sound is a name the notification bank owns") {
    CHECK(std::strcmp(kSurveyStartCueName, "AmiiboMarker_Sign") == 0);
    CHECK(kSurveyStartCueName[0] != '\0');
}
