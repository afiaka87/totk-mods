#pragma once
#include "pure/MatchedFormation.hpp"

namespace linked_stick::pure {
struct VehicleShape {
    std::uintptr_t assembly = 0;
    matched::Shape shape{};
    unsigned expected = 0;
    bool complete = false;
    bool valid() const {
        return complete && assembly && expected >= 2 && expected <= shape.members.size() &&
               shape.count == expected && matched::sameShape(shape, shape);
    }
};

inline const char* vehicleMatchFailure(const VehicleShape& controller, const VehicleShape& receiver) {
    if (!controller.valid()) return "controller_vehicle_unavailable";
    if (!receiver.valid()) return "receiver_vehicle_unavailable";
    if (controller.assembly == receiver.assembly) return "same_physical_vehicle";
    if (!matched::sameShape(controller.shape, receiver.shape)) return "different_vehicle_build";
    return nullptr;
}

inline std::uint64_t vehiclePartKind(const char* name) {
    std::uint64_t kind = 14695981039346656037ull;
    for (unsigned i = 0; i < 128 && name[i]; ++i) {
        kind ^= static_cast<unsigned char>(name[i]);
        kind *= 1099511628211ull;
    }
    return kind;
}
}
