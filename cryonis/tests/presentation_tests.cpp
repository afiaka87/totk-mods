// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "../src/pure/CryonisPresentation.hpp"
#include "../../runtime-support/include/totk/ui/NativeFont.hpp"
#include <thread>
#include <cstdio>
#include <cstdlib>
using cryonis::Presentation;
using totk::ui::native_font::Mailbox;
static Mailbox<Presentation> render, hud;
static std::atomic<bool> done{};
static void check(bool value) {
    if (!value) {
        std::fputs("Torn or lost presentation\n", stderr);
        std::abort();
    }
}
static void consume(Mailbox<Presentation> &mailbox) {
    float previous = 0;
    while (!done.load(std::memory_order_acquire)) {
        const auto snapshot = mailbox.consume();
        check(snapshot.elapsedFrames >= previous);
        previous = snapshot.elapsedFrames;
        for (const float height : snapshot.water)
            check(height == snapshot.elapsedFrames);
        if (snapshot.sceneReady)
            check(snapshot.pillars[0].occupied);
    }
}
int main() {
    std::thread r(consume, std::ref(render)), h(consume, std::ref(hud));
    Presentation frame;
    frame.sceneReady = true;
    frame.pillars[0].occupied = true;
    for (unsigned i = 1; i <= 10000; ++i) {
        frame.elapsedFrames = float(i);
        frame.running = (i % 3) != 0;
        for (auto &height : frame.water)
            height = float(i);
        render.publish(frame);
        hud.publish(frame);
    }
    done.store(true, std::memory_order_release);
    r.join();
    h.join();
    check(render.consume().elapsedFrames == 10000 && hud.consume().elapsedFrames == 10000);
    // No new simulation frame must retain the last solid pillar, even paused.
    frame.running = false;
    render.publish(frame);
    hud.publish(frame);
    for (unsigned i = 0; i < 100; ++i)
        check(render.consume().pillars[0].occupied && hud.consume().sceneReady);
    frame = {};
    render.publish(frame);
    hud.publish(frame);
    check(!render.consume().sceneReady && !hud.consume().sceneReady);
    std::puts("Two independent presentation readers: coherent frames, pause retention, scene reset "
              "passed");
}
