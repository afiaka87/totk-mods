
#include "RecallInput.hpp"

namespace self_recall::input {
namespace {
totk::engine::NpadReader g_reader{};
}  // namespace

totk::engine::NpadFrame read(void* device, InputState& state) {
    const totk::engine::NpadFrame frame = g_reader.read(device);
    if (frame.snapshot().freshSampleCount > 0) {
        state.latestStickX = frame.snapshot().leftStick.x;
        state.latestStickY = frame.snapshot().leftStick.y;
    }
    return frame;
}

}  // namespace self_recall::input
