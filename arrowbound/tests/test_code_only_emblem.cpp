// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <doctest.h>
#include "EmblemText.hpp"
#include "SettingsJournal.hpp"

using namespace arrowbound::pure;

TEST_CASE("literal messages match only the emblem keys in their exact tables") {
    CHECK(emblemMessage("ActorMsg/PouchContent", "Obj_CaveWellHonor_00_Name") == &kEmblemName);
    CHECK(emblemMessage("ActorMsg/PouchContent", "Obj_CaveWellHonor_00_Caption") == &kEmblemCaption);
    CHECK(emblemMessage("LayoutMsg/Pouch_00", "ArrowboundActivate") == &kActivate);
    CHECK(emblemMessage("LayoutMsg/Pouch_00", "ArrowboundDeactivate") == &kDeactivate);
    CHECK(emblemMessage("ActorMsg/PouchContent", "ArrowboundActivate") == nullptr);
    CHECK(emblemMessage("LayoutMsg/Pouch_00", "Obj_CaveWellHonor_00_Name") == nullptr);
    CHECK(emblemMessage("ActorMsg/PouchContent", "Obj_CaveWellHonor_00_NameExtra") == nullptr);
    CHECK(emblemMessage("ActorMsg/PouchContent", "NormalArrow_Name") == nullptr);
    CHECK(emblemMessage(nullptr, "ArrowboundActivate") == nullptr);
    CHECK(emblemMessage("LayoutMsg/Pouch_00", nullptr) == nullptr);
    CHECK(kEmblemName.length == 17);
    CHECK(kEmblemCaption.length == 73);
    CHECK(kEmblemCaption.text[kEmblemCaption.length] == 0);
    CHECK(kActivate.attributes == UINT64_MAX);
    CHECK(kDeactivate.padding == 0);
}

TEST_CASE("journal selects the newest valid record and alternates slots") {
    SettingsJournal journal;
    CHECK_FALSE(journal.enabled);
    CHECK(journal.slot == -1);
    auto first = settingsRecord(1, true);
    auto second = settingsRecord(2, false);
    journal.consider(second, 1);
    journal.consider(first, 0);
    CHECK_FALSE(journal.enabled);
    CHECK(journal.sequence == 2);
    CHECK(journal.slot == 1);
    SettingsRecord next{};
    int slot = -1;
    REQUIRE(journal.next(true, next, slot));
    CHECK(slot == 0);
    journal.consider(next, slot);
    CHECK(journal.sequence == 3);
    CHECK(journal.enabled);
    REQUIRE(journal.next(false, next, slot));
    CHECK(slot == 1);
}

TEST_CASE("every single-byte record corruption is rejected without losing the previous slot") {
    for (std::size_t byte = 0; byte < sizeof(SettingsRecord); ++byte) {
        auto corrupted = settingsRecord(2, false);
        corrupted[byte] ^= 0x80;
        SettingsJournal journal;
        journal.consider(settingsRecord(1, true), 0);
        journal.consider(corrupted, 1);
        CHECK(journal.sequence == 1);
        CHECK(journal.enabled);
    }
}

TEST_CASE("partial writes preserve the previous record and invalid files default off") {
    const auto replacement = settingsRecord(3, true);
    for (std::size_t length = 0; length < sizeof(SettingsRecord); ++length) {
        auto torn = settingsRecord(1, true);
        std::memcpy(torn.data(), replacement.data(), length);
        SettingsJournal journal;
        journal.consider(settingsRecord(2, false), 1);
        journal.consider(torn, 0);
        CHECK(journal.sequence == 2);
        CHECK_FALSE(journal.enabled);
    }
    SettingsJournal invalid;
    invalid.consider({}, 0);
    invalid.consider(settingsRecord(0, true), 1);
    CHECK_FALSE(invalid.enabled);
    CHECK(invalid.slot == -1);
    SettingsJournal exhausted;
    exhausted.consider(settingsRecord(UINT64_MAX, true), 0);
    SettingsRecord next{};
    int slot = -1;
    CHECK_FALSE(exhausted.next(false, next, slot));
}

namespace {
struct FakeStore {
    bool disk = false;
    bool fail = false;
    int loads = 0;
    int writes = 0;
    PersistentToggle* duringLoad = nullptr;
    PersistentToggle* duringSave = nullptr;
    bool load() {
        ++loads;
        if (duringLoad) duringLoad->request(false);
        return disk;
    }
    void save(bool enabled) {
        ++writes;
        if (!fail) disk = enabled;
        if (duringSave) { duringSave->request(false); duringSave = nullptr; }
    }
};
}

TEST_CASE("persistent toggle loads once and never rewinds on a world or save change") {
    PersistentToggle toggle;
    FakeStore store{true};
    toggle.service(store);
    CHECK(toggle.enabled());
    store.disk = false;
    for (int i = 0; i < 120; ++i) toggle.service(store);
    CHECK(toggle.enabled());
    CHECK(store.loads == 1);
    CHECK(store.writes == 0);
    toggle.request(false);
    CHECK_FALSE(toggle.enabled());
    toggle.service(store);
    CHECK(store.writes == 1);
}

TEST_CASE("menu choice beats a simultaneous initial settings load") {
    PersistentToggle toggle;
    FakeStore store{true};
    store.duringLoad = &toggle;
    toggle.service(store);
    CHECK_FALSE(toggle.enabled());
    CHECK_FALSE(store.disk);
    CHECK(store.writes == 1);
}

TEST_CASE("rapid choices coalesce and choices during a write are not lost") {
    PersistentToggle toggle;
    FakeStore store;
    toggle.service(store);
    toggle.request(true);
    toggle.request(false);
    toggle.request(true);
    store.duringSave = &toggle;
    toggle.service(store);
    CHECK(store.disk);
    CHECK_FALSE(toggle.enabled());
    toggle.service(store);
    CHECK_FALSE(store.disk);
    CHECK(store.writes == 2);
}

TEST_CASE("unwritable storage retains session choice without retrying every frame") {
    PersistentToggle toggle;
    FakeStore store;
    store.fail = true;
    toggle.service(store);
    toggle.request(true);
    for (int i = 0; i < 120; ++i) toggle.service(store);
    CHECK(toggle.enabled());
    CHECK_FALSE(store.disk);
    CHECK(store.writes == 1);
    store.fail = false;
    toggle.request(false);
    toggle.request(true);
    toggle.service(store);
    CHECK(store.disk);
    CHECK(store.writes == 2);
}
