// SPDX-License-Identifier: MIT
#include <doctest.h>
#include "LabelPlacement.hpp"

using namespace zonai_survey;
namespace {
constexpr float kFrame = 1 / 60.f;
atlas::Label label(std::uint16_t name, float worldX, float distanceSq, float x, float nameWidth) {
    return {name, worldX, 0, distanceSq, {x, 100, x + 16, 120}, nameWidth > 0 ? x + 16 + nameWidth : 0};
}
}

TEST_CASE("a name at another label's edge holds steady while the view wobbles a pixel") {
    // The near label's name ends at x=176; the far label wobbles one unit either side of touching it.
    const float starts[]{177.f, 240.f};
    for (const float start : starts) {
        atlas::NamePlacement names;
        float alpha[2]{};
        unsigned changes = 0;
        const atlas::Label first[]{label(1, 0, 1, 100, 60), label(2, 10, 4, start, 60)};
        names.place(first, 2, kFrame, alpha);
        const float initial = alpha[1];
        for (int frame = 0; frame < 240; ++frame) {
            const float wobble = frame % 2 ? 1.f : -1.f;
            const atlas::Label labels[]{label(1, 0, 1, 100, 60), label(2, 10, 4, 177 + wobble, 60)};
            changes += names.place(labels, 2, kFrame, alpha);
        }
        // Starting against the edge it stays hidden; starting clear it keeps its name.
        CHECK(changes == 0);
        CHECK(alpha[0] == 1);
        CHECK(alpha[1] == initial);
        CHECK(initial == (start > 200 ? 1.f : 0.f));
    }
}

TEST_CASE("labels at swapping distances do not trade a name") {
    atlas::NamePlacement names;
    float alpha[2]{};
    unsigned changes = 0;
    for (int frame = 0; frame < 120; ++frame) {
        const bool firstNearer = frame % 2 == 0;
        const atlas::Label labels[]{label(1, 0, firstNearer ? 1.f : 2.f, 100, 60),
                                    label(2, 10, firstNearer ? 2.f : 1.f, 104, 60)};
        changes += names.place(labels, 2, kFrame, alpha);
    }
    CHECK(changes == 0);
    CHECK(alpha[0] == 1);
    CHECK(alpha[1] == 0);
}

TEST_CASE("a hidden name returns after its space stays clear, then fades in") {
    atlas::NamePlacement names;
    float alpha[2]{};
    const atlas::Label crowded[]{label(1, 0, 1, 100, 60), label(2, 10, 4, 104, 60)};
    names.place(crowded, 2, kFrame, alpha);
    CHECK(alpha[1] == 0);
    // The near label leaves; the far label keeps the same place in the list.
    const atlas::Label alone[]{label(2, 10, 4, 104, 60)};
    int frames = 0;
    unsigned changes = 0;
    while (changes == 0 && frames < 120) { changes += names.place(alone, 1, kFrame, alpha); ++frames; }
    CHECK(changes == 1);
    CHECK(frames * kFrame >= doctest::Approx(atlas::kNameReturnSeconds));
    CHECK(frames * kFrame < atlas::kNameReturnSeconds + 2 * kFrame);
    CHECK(alpha[0] > 0);
    CHECK(alpha[0] < 0.1f);
    for (int frame = 0; frame < 12; ++frame) names.place(alone, 1, kFrame, alpha);
    CHECK(alpha[0] == 1);
}

TEST_CASE("a name clearly covered hides at once and fades out") {
    atlas::NamePlacement names;
    float alpha[2]{};
    const atlas::Label apart[]{label(1, 0, 1, 100, 60), label(2, 10, 4, 400, 60)};
    names.place(apart, 2, kFrame, alpha);
    CHECK(alpha[1] == 1);
    const atlas::Label covered[]{label(1, 0, 1, 100, 60), label(2, 10, 4, 120, 60)};
    CHECK(names.place(covered, 2, kFrame, alpha) == 1);
    CHECK(alpha[1] < 1);
    CHECK(alpha[1] > 0.8f);
    for (int frame = 0; frame < 12; ++frame) names.place(covered, 2, kFrame, alpha);
    CHECK(alpha[1] == 0);
    CHECK(alpha[0] == 1);
}

TEST_CASE("new labels with room show names at once and nameless labels never do") {
    atlas::NamePlacement names;
    float alpha[3]{};
    const atlas::Label labels[]{label(1, 0, 1, 100, 60), label(2, 10, 4, 400, 60), label(3, 20, 9, 700, 0)};
    CHECK(names.place(labels, 3, kFrame, alpha) == 0);
    CHECK(alpha[0] == 1);
    CHECK(alpha[1] == 1);
    CHECK(alpha[2] == 0);
}

TEST_CASE("a label that moves a little keeps its state and one that jumps far is new") {
    atlas::NamePlacement names;
    float alpha[2]{};
    const atlas::Label crowded[]{label(1, 0, 1, 100, 60), label(2, 10, 4, 104, 60)};
    names.place(crowded, 2, kFrame, alpha);
    const atlas::Label moved[]{label(1, 0, 1, 100, 60), label(2, 11.5f, 4, 400, 60)};
    names.place(moved, 2, kFrame, alpha);
    CHECK(alpha[1] == 0);  // same label, still waiting out the return delay
    const atlas::Label jumped[]{label(1, 0, 1, 100, 60), label(2, 30, 4, 400, 60)};
    names.place(jumped, 2, kFrame, alpha);
    CHECK(alpha[1] == 1);  // a different label with room
}
