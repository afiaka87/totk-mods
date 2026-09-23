#pragma once
#include <cstdint>
namespace nn::os {
inline std::uint64_t GetSystemTickFrequency() { return 1000000; }
}
