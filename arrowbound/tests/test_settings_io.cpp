// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include <nn/fs.h>
#include "ArrowSettings.hpp"
#include "SettingsJournal.hpp"

namespace {
arrowbound::pure::SettingsJournal diskJournal() {
    arrowbound::pure::SettingsJournal journal;
    arrowbound::pure::SettingsRecord record;
    for (int slot = 0; slot < 2; ++slot) {
        std::memcpy(record.data(), nn::fs::fake::bytes.data() + slot * record.size(), record.size());
        journal.consider(record, slot);
    }
    return journal;
}
}

TEST_CASE("real settings adapter preserves records and handles native IO failures") {
    namespace settings = arrowbound::settings;
    namespace fs = nn::fs::fake;
    fs::exists = false;
    settings::service();
    CHECK_FALSE(settings::enabled());
    CHECK(fs::writes == 0);
    settings::request(true);
    settings::service();
    CHECK(settings::enabled());
    REQUIRE(fs::writes == 1);
    CHECK(diskJournal().enabled);
    CHECK(diskJournal().sequence == 1);
    CHECK(fs::mounts == 1);

    fs::failWrite = true;
    settings::request(false);
    settings::service();
    CHECK_FALSE(settings::enabled());
    CHECK(diskJournal().enabled);
    CHECK(diskJournal().sequence == 1);
    settings::service();
    CHECK(fs::writes == 2);

    fs::failWrite = false;
    settings::request(true);
    settings::service();
    CHECK(diskJournal().sequence == 2);
    fs::failRead = true;
    settings::request(false);
    settings::service();
    CHECK_FALSE(diskJournal().enabled);
    fs::failRead = false;
    settings::request(true);
    settings::service();
    CHECK(diskJournal().enabled);
    CHECK(diskJournal().sequence == 3);

    const auto before = fs::bytes;
    const auto writes = fs::writes;
    fs::size = 47;
    settings::request(false);
    settings::service();
    CHECK_FALSE(settings::enabled());
    CHECK(fs::bytes == before);
    CHECK(fs::writes == writes);
    fs::size = 48;
    settings::request(true);
    settings::service();
    CHECK(diskJournal().sequence == 4);
    CHECK_FALSE(fs::wrongPath);
    CHECK_FALSE(fs::flushMissing);
    CHECK(fs::closes == 7);
}
