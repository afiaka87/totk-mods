#pragma once

#include <cstdint>

namespace linked_stick::pure {
enum class LinkAction : std::uint8_t { None, Controller, Receiver, Clear };

class LinkInput {
public:
    static constexpr std::uint64_t clicks = (1ull << 4) | (1ull << 5);
    static constexpr std::uint64_t foreign = (0xFull << 6) | (3ull << 10);
    static constexpr std::uint64_t holdMs = 1500;

    LinkAction step(std::uint64_t buttons, bool available, bool haveController,
                    std::uint64_t nowMs) {
        const bool completedChord = (buttons & clicks) == clicks && (previous_ & clicks) != clicks;
        previous_ = buttons;
        owned_ &= buttons;
        const bool interrupted = nowMs < lastAt_ || nowMs - lastAt_ > 250;
        lastAt_ = nowMs;
        if (!available || (buttons & foreign)) {
            gesturing_ = false;
            if (buttons & foreign) owned_ = 0;
            return LinkAction::None;
        }
        const bool extraButton = (buttons & (0xFFFFull & ~clicks)) != 0;
        if (interrupted || extraButton) gesturing_ = false;
        if (completedChord && !extraButton) {
            gesturing_ = true;
            startedAt_ = nowMs;
            owned_ |= clicks;
        }
        if (!gesturing_) return LinkAction::None;
        if (!(buttons & clicks)) {
            gesturing_ = false;
            return haveController ? LinkAction::Receiver : LinkAction::Controller;
        }
        if ((buttons & clicks) == clicks && nowMs - startedAt_ >= holdMs) {
            gesturing_ = false;
            return LinkAction::Clear;
        }
        return LinkAction::None;
    }

    std::uint64_t ownedButtons() const { return owned_; }

private:
    std::uint64_t previous_ = 0, owned_ = 0, startedAt_ = 0, lastAt_ = 0;
    bool gesturing_ = false;
};
}
