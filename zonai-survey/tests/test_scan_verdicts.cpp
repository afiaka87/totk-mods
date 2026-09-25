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
    ScanVerdict::AlreadyPulsing,
    ScanVerdict::PlayerUnresolved,
};

constexpr ScanAbandonReason kAllEndings[] = {
    ScanAbandonReason::PlayerLost,
    ScanAbandonReason::WorldReloaded,
    ScanAbandonReason::Expired,
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

TEST_CASE("every verdict has player-facing text, never a null or a blank") {
    for (const ScanVerdict verdict : kAllVerdicts) {
        CHECK(isPlainSentence(displayText(verdict)));
    }
    for (const ScanAbandonReason reason : kAllEndings) {
        CHECK(isPlainSentence(displayText(reason)));
    }
}

TEST_CASE("the font can render every refusal — ASCII only, no punctuation surprises") {

    for (const ScanVerdict verdict : kAllVerdicts) {
        const std::string text = displayText(verdict);
        CHECK(text.find("\xe2") == std::string::npos);
    }
    for (const ScanAbandonReason reason : kAllEndings) {
        const std::string text = displayText(reason);
        CHECK(text.find("\xe2") == std::string::npos);
    }
}

TEST_CASE("no two refusals read the same, or the banner cannot tell them apart") {
    for (const ScanVerdict a : kAllVerdicts) {
        for (const ScanVerdict b : kAllVerdicts) {
            if (a == b) continue;
            CHECK(std::string(displayText(a)) != std::string(displayText(b)));
        }
    }
    for (const ScanAbandonReason a : kAllEndings) {
        for (const ScanAbandonReason b : kAllEndings) {
            if (a == b) continue;
            CHECK(std::string(displayText(a)) != std::string(displayText(b)));
        }
    }
}

TEST_CASE("an unmapped enum value still produces text rather than falling off the end") {

    const auto strayVerdict = static_cast<ScanVerdict>(200);
    const auto strayReason = static_cast<ScanAbandonReason>(200);
    CHECK(isPlainSentence(displayText(strayVerdict)));
    CHECK(isPlainSentence(displayText(strayReason)));
}

TEST_CASE("only a completed fade is an ordinary ending") {

    CHECK(isOrdinaryEnding(ScanAbandonReason::Expired));
    CHECK_FALSE(isOrdinaryEnding(ScanAbandonReason::PlayerLost));
    CHECK_FALSE(isOrdinaryEnding(ScanAbandonReason::WorldReloaded));
}

TEST_CASE("a refusal is a value, so gameplay never compares rendered strings") {

    const ScanVerdict verdict = ScanVerdict::AlreadyPulsing;
    CHECK(verdict != ScanVerdict::Accepted);
    CHECK(static_cast<int>(ScanVerdict::Accepted) == 0);
}

TEST_CASE("the wording uses the player's frame, not the code's") {

    CHECK(std::string(displayText(ScanVerdict::AlreadyPulsing)) ==
          "a scan is already travelling");
    CHECK(std::string(displayText(ScanVerdict::PlayerUnresolved)) ==
          "cannot find Link right now");
    CHECK(std::string(displayText(ScanAbandonReason::PlayerLost)) == "lost Link mid-scan");
    CHECK(std::string(displayText(ScanAbandonReason::WorldReloaded)) == "the world reloaded");
    CHECK(std::string(displayText(ScanAbandonReason::Expired)) == "scan faded out");
}
