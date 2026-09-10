#include <array>
#include "RecallRouteProbePlan.hpp"
#include "RecallVehiclePolicy.hpp"
#include "doctest.h"

using namespace self_recall::pure;

TEST_CASE("steering ownership ends for the same actor and cannot leak across replacement") {
    NativeVehicleState state;
    CHECK_FALSE(state.active(0));
    CHECK_FALSE(state.active(41));
    state.enter(41);
    CHECK(state.active(41));
    CHECK_FALSE(state.active(42));
    state.enter(42);
    state.leave(41);
    CHECK(state.active(42));
    state.leave(42);
    CHECK_FALSE(state.active(42));
    state.enter(42);
    state.clear();
    CHECK_FALSE(state.active(42));
}

TEST_CASE("steering entry and exit between input and actor phases leave no unsafe history hole") {
    std::array<HistorySample, 5> history{};
    for (unsigned i = 0; i < history.size(); ++i) {
        history[i].pose.rotation.values[0] = 1;
        history[i].pose.rotation.values[4] = 1;
        history[i].pose.rotation.values[8] = 1;
        history[i].pose.position.x = static_cast<float>(i);
    }
    history[0].flags = SampleAdmissible;
    history[1].flags = pairVehicleAdmission(SampleAdmissible | SampleControlStick, true, false);
    history[2].flags = pairVehicleAdmission(SampleAdmissible, true, true);
    history[3].flags = pairVehicleAdmission(0, true, true);
    history[4].flags = SampleAdmissible;
    for (const auto& sample : history) CHECK((sample.flags & SampleAdmissible) != 0);
    const auto route = planRouteProbe(history, false);
    CHECK(route.kind == RouteProbeKind::Vehicle);
    CHECK(route.through == 4);
    CHECK(pairVehicleAdmission(0, false, true) == 0);
    CHECK(pairVehicleAdmission(0, true, false) == 0);
    history[3].flags = 0;
    CHECK(planRouteProbe(std::span(history).subspan(2), false).kind == RouteProbeKind::Unavailable);
}
