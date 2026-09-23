#pragma once

#include "pure/LinkInput.hpp"

namespace linked_stick::pure {
// Use known one-shots; AmiiboMarker_OK loops.
constexpr const char* selectionCue(LinkAction action, bool accepted) {
    if (action == LinkAction::None) return nullptr;
    if (!accepted) return "mc_AmiiboError";
    if (action == LinkAction::Controller) return "MapMarker_1";
    if (action == LinkAction::Receiver) return "mc_HeartUp_Short";
    if (action == LinkAction::Clear) return "AmiiboMarker_Sign";
    return nullptr;
}
}
