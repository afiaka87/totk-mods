// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "engine/RaycastService.hpp"

#include <cstdio>
#include <cstring>

namespace {

int gFailures = 0;

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                              \
            ++gFailures;                                                                            \
        }                                                                                           \
    } while (false)

template <typename Value>
void writeValue(unsigned char* bytes, int offset, Value value) {
    std::memcpy(bytes + offset, &value, sizeof(value));
}

void testProducerWorkerConsumer() {
    bivouac::engine::RaycastService casts;
    const float from[3] = {1.0f, 2.0f, 3.0f};
    const float to[3] = {4.0f, 5.0f, 6.0f};
    CHECK(casts.request(from, to, 0x20, 10));
    CHECK(!casts.request(from, to, 0x20, 10));
    CHECK(casts.requestPending());
    CHECK(casts.requestFrom()[1] == 2.0f);
    CHECK(casts.requestTo()[2] == 6.0f);

    unsigned char workerObject[0x200] = {};
    writeValue<std::uint32_t>(workerObject, 0x20, 1);
    writeValue<float>(workerObject, 0x24, 7.0f);
    writeValue<float>(workerObject, 0x28, 8.0f);
    writeValue<float>(workerObject, 0x2C, 9.0f);
    writeValue<float>(workerObject, 0x30, -1.0f);
    writeValue<float>(workerObject, 0x34, 0.0f);
    writeValue<float>(workerObject, 0x38, 1.0f);
    writeValue<float>(workerObject, 0x40, 12.5f);
    writeValue<std::uint32_t>(workerObject, 0x120, 44);

    casts.prepareWorkerObject(workerObject);
    casts.publishWorkerResult();
    CHECK(!casts.requestPending());
    const auto result = casts.poll(11, 8);
    CHECK(result.resolved && !result.timedOut);
    CHECK(result.hit == 1);
    CHECK(result.distance == 12.5f);
    CHECK(result.position[2] == 9.0f);
    CHECK(result.normal[0] == -1.0f);
    CHECK(result.bodyId == 44);
    CHECK(casts.request(from, to, 0x20, 12));
}

void testWorkerLivenessAndTimeout() {
    bivouac::engine::RaycastService casts;
    const float point[3] = {};
    CHECK(casts.request(point, point, 0x20, 100));
    CHECK(!casts.observeWorkerForTick());
    CHECK(!casts.poll(200, 8).resolved); // Paused worker freezes the timeout.

    casts.noteWorkerCall();
    CHECK(casts.observeWorkerForTick());
    const auto timeout = casts.poll(200, 8);
    CHECK(timeout.resolved && timeout.timedOut);

    CHECK(casts.request(point, point, 0x20, 201));
    casts.cancel();
    CHECK(!casts.requestPending());
    CHECK(casts.request(point, point, 0x20, 202));
}

} // namespace

int main() {
    testProducerWorkerConsumer();
    testWorkerLivenessAndTimeout();
    if (gFailures == 0) {
        std::puts("raycast service tests: PASS");
        return 0;
    }
    std::printf("raycast service tests: %d failure(s)\n", gFailures);
    return 1;
}
