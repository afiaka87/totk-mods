#pragma once

#include <atomic>
#include <cstdint>
#include "RecallFrameTicket.hpp"

namespace self_recall::pure {

inline std::uint8_t pairNativeClimbAdmission(std::uint8_t flags, bool mayAdmit, bool nativeClimb) {
    return mayAdmit && nativeClimb
         ? static_cast<std::uint8_t>(flags | SampleClimb | SampleAdmissible) : flags;
}

class NativeTraversalState {
public:
    void enterFall(std::uint32_t actorId) { fall_.store(key(actorId)); }
    void enterGlide(std::uint32_t actorId) { glide_.store(key(actorId)); }
    void enterClimb(std::uint32_t actorId) { climb_.store(key(actorId)); }
    void leaveFall(std::uint32_t actorId) { auto expected = key(actorId); fall_.compare_exchange_strong(expected, 0); }
    void leaveGlide(std::uint32_t actorId) { auto expected = key(actorId); glide_.compare_exchange_strong(expected, 0); }
    void leaveClimb(std::uint32_t actorId) { auto expected = key(actorId); climb_.compare_exchange_strong(expected, 0); }
    bool falling(std::uint32_t actorId) const { return fall_.load() == key(actorId); }
    bool gliding(std::uint32_t actorId) const { return glide_.load() == key(actorId); }
    bool climbing(std::uint32_t actorId) const { return climb_.load() == key(actorId); }
    void clear() { fall_.store(0); glide_.store(0); climb_.store(0); }
private:
    static constexpr std::uint64_t key(std::uint32_t id) { return std::uint64_t{id} + 1; }
    std::atomic<std::uint64_t> fall_{0}, glide_{0}, climb_{0};
};

struct GliderReleaseContext {
    std::uintptr_t actor = 0;
    std::uint32_t actorId = 0;
    std::uint32_t worldGeneration = 0;
    std::uint64_t tick = 0;
    bool allowed = false;
};

enum class GliderReleaseEnd : unsigned {
    None, Entered, Cancelled, ContextChanged, TimedOut, NewRecall, Unavailable,
};

struct GliderReleaseResult {
    std::uint64_t serial = 0;
    GliderReleaseEnd reason = GliderReleaseEnd::None;
    std::uint32_t forcedCalls = 0;
};

class GliderRelease {
public:
    static constexpr std::uint64_t kAcquireTicks = 45;

    std::uint64_t arm(const GliderReleaseContext& context, bool recordedNativeGlide,
                      const NativeTraversalState& traversal) {
        cancel(GliderReleaseEnd::Cancelled);
        if (!recordedNativeGlide || !context.allowed || !context.actor || !context.worldGeneration ||
            traversal.gliding(context.actorId) || context.tick > UINT64_MAX - kAcquireTicks ||
            next_ == (UINT64_MAX >> 16)) return 0;
        Request request{context, ++next_};
        if (!request_.publish(request)) return 0;
        owned_ = request;
        now_.store(context.tick);
        forced_.store(request.serial << 16);
        active_.store(request.serial);
        return request.serial;
    }

    void service(const GliderReleaseContext& context, bool userCancelled) {
        now_.store(context.tick);
        const auto serial = active_.load();
        if (!serial) return;
        if (userCancelled) { finish(serial, GliderReleaseEnd::Cancelled); return; }
        const auto& started = owned_.context;
        if (owned_.serial != serial || !context.allowed || context.actor != started.actor ||
            context.actorId != started.actorId || context.worldGeneration != started.worldGeneration ||
            context.tick < started.tick) {
            finish(serial, GliderReleaseEnd::ContextChanged);
        } else if (context.tick - started.tick >= kAcquireTicks) {
            finish(serial, GliderReleaseEnd::TimedOut);
        }
    }

    std::uint64_t forceTicket(std::uintptr_t actor, std::uint32_t actorId,
                              const NativeTraversalState& traversal) {
        Request request;
        const auto serial = active_.load();
        if (!serial || !request_.snapshot(request) || request.serial != serial ||
            request.context.actor != actor || request.context.actorId != actorId ||
            !traversal.falling(actorId) || traversal.gliding(actorId)) return 0;
        const auto now = now_.load();
        if (now < request.context.tick || now - request.context.tick >= kAcquireTicks ||
            active_.load() != serial) return 0;
        return serial;
    }

    bool confirmForce(std::uint64_t serial) {
        auto value = forced_.load();
        while (serial && active_.load() == serial && (value >> 16) == serial) {
            if ((value & 0xFFFFu) == 0xFFFFu || forced_.compare_exchange_weak(value, value + 1))
                return active_.load() == serial;
        }
        return false;
    }

    void entered(std::uintptr_t actor, std::uint32_t actorId) {
        Request request;
        const auto serial = active_.load();
        if (serial && request_.snapshot(request) && request.serial == serial &&
            request.context.actor == actor && request.context.actorId == actorId)
            finish(serial, GliderReleaseEnd::Entered);
    }

    void cancel(GliderReleaseEnd reason = GliderReleaseEnd::Cancelled) { finish(active_.load(), reason); }
    void cancelTicket(std::uint64_t serial, GliderReleaseEnd reason) { finish(serial, reason); }
    bool pending() const { return active_.load() != 0; }
    GliderReleaseResult result() const {
        const auto terminal = terminal_.load();
        const auto serial = terminal >> 8;
        const auto forced = forced_.load();
        return {serial, static_cast<GliderReleaseEnd>(terminal & 0xFFu),
                (forced >> 16) == serial ? static_cast<std::uint32_t>(forced & 0xFFFFu) : 0};
    }

private:
    struct Request { GliderReleaseContext context{}; std::uint64_t serial = 0; };
    void finish(std::uint64_t serial, GliderReleaseEnd reason) {
        if (!serial || !active_.compare_exchange_strong(serial, 0)) return;
        const auto terminal = (serial << 8) | static_cast<unsigned>(reason);
        auto prior = terminal_.load();
        while (prior < terminal && !terminal_.compare_exchange_weak(prior, terminal)) {}
    }
    FrameMailbox<Request> request_;
    Request owned_{};
    std::uint64_t next_ = 0;
    std::atomic<std::uint64_t> active_{0}, now_{0}, forced_{0}, terminal_{0};
};

} // namespace self_recall::pure
