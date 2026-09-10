#include <atomic>
#include <limits>
#include <thread>

#include "RecallFrameTicket.hpp"
#include "doctest.h"

using namespace self_recall::pure;

TEST_CASE("pose ticket rejects recycled actors and mismatched movement") {
    ActorFrameTicket ticket{};
    ticket.actor = 100;
    ticket.actorId = 7;
    ticket.model = 200;
    ticket.worldGeneration = 3;
    ticket.route.pose.rotation.values[0] = 1;
    ticket.route.pose.rotation.values[4] = 1;
    ticket.route.pose.rotation.values[8] = 1;
    ticket.route.pose.position = {3, 4, 5};
    ticket.modelRoot[0] = ticket.modelRoot[5] = ticket.modelRoot[10] = 1;
    ticket.modelRoot[3] = 3;
    ticket.modelRoot[7] = 4;
    ticket.modelRoot[11] = 5;
    const auto matches = [&](const ActorFrameTicket& current) {
        return matchesActorFrame(ticket, current.actor, current.actorId, current.model,
                                 current.worldGeneration, current.route.pose, current.modelRoot);
    };
    CHECK(matches(ticket)); // stationary root can accompany a new animation frame
    auto changed = ticket;
    ++changed.actorId;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    ++changed.worldGeneration;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    ++changed.model;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    changed.route.pose.position.x += 1;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    changed.route.pose.rotation.values[0] = -1;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    changed.modelRoot[7] += 1;
    CHECK_FALSE(matches(changed));
    changed = ticket;
    changed.modelRoot[5] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(matches(changed));
    CHECK_FALSE(matchesActorFrame(ticket, 100, 7, 200, 3, ticket.route.pose, nullptr));
}

TEST_CASE("frame mailbox never mixes concurrently published frame fields") {
    struct Frame { std::uint64_t serial = 0; std::uint64_t fields[24]{}; };
    FrameMailbox<Frame> mailbox;
    std::atomic<bool> done{false};
    std::atomic<unsigned> incoherent{0};
    std::atomic<unsigned> observed{0};
    std::thread reader([&] {
        do {
            Frame frame;
            if (!mailbox.snapshot(frame) || !frame.serial) continue;
            for (const auto value : frame.fields)
                if (value != frame.serial) ++incoherent;
            ++observed;
        } while (!done.load(std::memory_order_acquire));
    });
    for (std::uint64_t i = 1; i <= 10000; ++i) {
        Frame frame;
        frame.serial = i;
        for (auto& value : frame.fields) value = i;
        while (!mailbox.publish(frame)) std::this_thread::yield();
    }
    while (!observed.load()) std::this_thread::yield();
    done.store(true, std::memory_order_release);
    reader.join();
    CHECK(incoherent.load() == 0);
    Frame finalFrame;
    REQUIRE(mailbox.snapshot(finalFrame));
    CHECK(finalFrame.serial == 10000);
}
