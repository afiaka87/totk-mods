#pragma once
#include <array>
#include <cstdint>

namespace self_recall::pure {
enum class PlaybackRate : std::uint8_t { Normal = 4, X125 = 5, X150 = 6, X175 = 7, X200 = 8, X400 = 16 };
inline constexpr std::array kPlaybackRates{PlaybackRate::Normal, PlaybackRate::X125,
    PlaybackRate::X150, PlaybackRate::X175, PlaybackRate::X200, PlaybackRate::X400};
inline bool validPlaybackRate(PlaybackRate rate) {
    for (const auto candidate : kPlaybackRates) if (candidate == rate) return true;
    return false;
}
inline const char* playbackRateText(PlaybackRate rate) {
    switch (rate) {
        case PlaybackRate::Normal: return "1.00x";
        case PlaybackRate::X125: return "1.25x";
        case PlaybackRate::X150: return "1.50x";
        case PlaybackRate::X175: return "1.75x";
        case PlaybackRate::X200: return "2.00x";
        case PlaybackRate::X400: return "4.00x";
    }
    return "invalid";
}

} // namespace self_recall::pure
