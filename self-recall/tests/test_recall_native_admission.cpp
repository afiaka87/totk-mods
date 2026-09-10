#include <array>
#include "RecallNativeAdmission.hpp"
#include "doctest.h"

using namespace self_recall;

namespace {
template<class T, std::size_t N>
void write(std::array<std::byte, N>& target, unsigned offset, T value) {
    std::memcpy(target.data() + offset, &value, sizeof(value));
}

struct AdmissionFixture {
    std::array<std::byte, 0x42D8> scene{};
    std::array<std::byte, 0x244> body{}, glider{};
    std::array<std::uintptr_t, 3> units{11, 22, 33};
    std::array<const void*, 1> bodyEntries{&units[0]};
    std::array<const void*, 2> gliderEntries{&units[1], &units[2]};
    std::array<const void*, 2> roots{body.data(), glider.data()};
    std::array<pure::RecordedModelPose, 3> models{};
    AdmissionFixture() {
        write(scene, 0x42A8, std::uint8_t{1});
        write(scene, 0x42C4, 4);
        write(scene, 0x42D4, 4);
        write(body, 0x60, scene.data());
        write(glider, 0x60, scene.data());
        write(body, 0x20, 1);
        write(glider, 0x20, 2);
        write(body, 0x28, bodyEntries.data());
        write(glider, 0x28, gliderEntries.data());
        for (unsigned i = 0; i < models.size(); ++i) {
            models[i].identity.unit = units[i];
            models[i].queueAdmission = 1;
        }
    }
    auto plan() { return model::planNativeAdmission(scene.data(), roots, models); }
};
}

TEST_CASE("historically drawn retained models enter their native queue lanes once") {
    AdmissionFixture fixture;
    const auto beforeBody = fixture.body;
    const auto beforeGlider = fixture.glider;
    const auto beforeScene = fixture.scene;
    auto plan = fixture.plan();
    REQUIRE(plan.status == model::AdmissionStatus::Ready);
    CHECK(plan.count == 2);
    CHECK(plan.request[0] == fixture.body.data());
    CHECK(plan.request[1] == fixture.glider.data());
    CHECK(fixture.body == beforeBody);
    CHECK(fixture.glider == beforeGlider);
    CHECK(fixture.scene == beforeScene);
    write(fixture.body, 0x241, std::uint8_t{2}); // Already requested by live gameplay.
    plan = fixture.plan();
    REQUIRE(plan.status == model::AdmissionStatus::Ready);
    CHECK(plan.count == 1);
    CHECK(plan.request[0] == fixture.glider.data());
    fixture.models[1].queueAdmission = fixture.models[2].queueAdmission = 0;
    CHECK(fixture.plan().count == 0);
}

TEST_CASE("native admission rejects destroyed roots and exhausted or closed queues without mutation") {
    AdmissionFixture fixture;
    write(fixture.glider, 0x241, std::uint8_t{1});
    CHECK(fixture.plan().status == model::AdmissionStatus::DetachedModel);
    write(fixture.glider, 0x241, std::uint8_t{0});
    write(fixture.glider, 0x240, std::uint8_t{8});
    CHECK(fixture.plan().status == model::AdmissionStatus::DetachedModel);
    write(fixture.glider, 0x240, std::uint8_t{0});
    write(fixture.scene, 0x42D0, 4);
    CHECK(fixture.plan().status == model::AdmissionStatus::QueueFull);
    write(fixture.glider, 0x241, std::uint8_t{2});
    CHECK(fixture.plan().status == model::AdmissionStatus::Ready);
    write(fixture.scene, 0x42A8, std::uint8_t{0});
    CHECK(fixture.plan().status == model::AdmissionStatus::ClosedQueue);
}

TEST_CASE("native admission requires current scene ownership and every recorded unit") {
    AdmissionFixture fixture;
    write(fixture.glider, 0x60, static_cast<const void*>(nullptr));
    CHECK(fixture.plan().status == model::AdmissionStatus::WrongScene);
    write(fixture.glider, 0x60, fixture.scene.data());
    fixture.models[2].identity.unit = 99;
    CHECK(fixture.plan().status == model::AdmissionStatus::MissingUnit);
    fixture.models[2].identity.unit = 33;
    fixture.roots[1] = fixture.roots[0];
    CHECK(fixture.plan().status == model::AdmissionStatus::InvalidRoster);
}
