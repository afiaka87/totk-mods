#pragma once
#include <array>
#include <cstdint>

namespace linked_stick::pure {
inline constexpr std::uint32_t kPassThroughLayers =
    (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4) | (1u << 5) |
    (1u << 6) | (1u << 11) | (1u << 23) | (1u << 25);
struct CollisionBody {
    std::uintptr_t world = 0, body = 0;
    std::uint64_t id = 0;
    bool operator==(const CollisionBody&) const = default;
};
class ObstaclePassThrough {
   public:
    static constexpr unsigned capacity = 84;
    using Bodies = std::array<CollisionBody, capacity>;
    // Resolve generation-bearing IDs in the current world before touching or restoring bodies.
    template <class Backend>
    void update(std::uintptr_t world, const Bodies& desired, unsigned count, Backend& backend) {
        count = count > capacity ? capacity : count;
        for (auto& lease : leases_) {
            if (!lease.used) continue;
            std::uint32_t current = 0;
            if (lease.body.world != world || !backend.readCollisionMask(lease.body, current)) {
                backend.collisionEvent("retired", lease.body, lease.removed, 0);
                lease = {};
                continue;
            }
            bool wanted = false;
            for (unsigned i = 0; i < count; ++i) wanted |= desired[i] == lease.body;
            if (wanted) {
                lease.removed |= current & kPassThroughLayers;
                const auto target = current & ~kPassThroughLayers;
                if (target != current) backend.writeCollisionMask(lease.body, target);
            } else {
                const auto target = current | lease.removed;
                if (target == current) {
                    backend.collisionEvent("restored", lease.body, current, target);
                    lease = {};
                } else if (backend.writeCollisionMask(lease.body, target)) {
                    // Keep leases until post-drain readback confirms restoration.
                    backend.collisionEvent("restore queued", lease.body, current, target);
                }
            }
        }
        for (unsigned i = 0; i < count; ++i) {
            const auto body = desired[i];
            bool exists = false;
            for (const auto& lease : leases_) exists |= lease.used && lease.body == body;
            if (exists) continue;
            std::uint32_t current = 0;
            if (body.world != world || !backend.readCollisionMask(body, current)) {
                backend.collisionEvent("admission refused", body, 0, 0);
                continue;
            }
            Lease* free = nullptr;
            for (auto& lease : leases_) if (!lease.used) { free = &lease; break; }
            if (!free) { backend.collisionEvent("capacity refused", body, capacity, count); continue; }
            *free = {body, current & kPassThroughLayers, true};
            const auto target = current & ~kPassThroughLayers;
            if (target != current) backend.writeCollisionMask(body, target);
            backend.collisionEvent("enabled", body, current, target);
        }
    }
    unsigned size() const {
        unsigned n = 0;
        for (const auto& lease : leases_) n += lease.used ? 1u : 0u;
        return n;
    }
   private:
    struct Lease { CollisionBody body{}; std::uint32_t removed = 0; bool used = false; };
    std::array<Lease, capacity> leases_{};
};
}
