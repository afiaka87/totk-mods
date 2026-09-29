// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#include <cstring>
#include <string>

#include "ScanVerdicts.hpp"
#include "ScanPresentation.hpp"
#include "doctest.h"

using namespace zonai_survey::pure;
using zonai_survey::presentation::displayText;

namespace {

constexpr ScanVerdict kAllVerdicts[] = {
    ScanVerdict::Accepted,
    ScanVerdict::PlayerUnresolved,
    ScanVerdict::CoolingDown,
};

bool isPlainSentence(const char* text) {
    if (text == nullptr) return false;
    const std::size_t length = std::strlen(text);
    if (length == 0 || length > 48) return false;
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

}

TEST_CASE("every verdict has distinct printable ASCII text for the banner") {
    for (const ScanVerdict a : kAllVerdicts) {
        CHECK(isPlainSentence(displayText(a)));
        for (const ScanVerdict b : kAllVerdicts) {
            if (a != b) CHECK(std::string(displayText(a)) != std::string(displayText(b)));
        }
    }
    CHECK(isPlainSentence(displayText(static_cast<ScanVerdict>(200))));
}

TEST_CASE("the wording uses the player's frame, not the code's") {
    CHECK(static_cast<int>(ScanVerdict::Accepted) == 0);
    CHECK(std::string(displayText(ScanVerdict::PlayerUnresolved)) == "cannot find Link right now");
}
