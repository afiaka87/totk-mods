// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
namespace cryonis::engine {
void configurePrompt(std::uintptr_t base);
// A pane is borrowed for one synchronous draw; restore visibility before returning.
class NativePrompt {
  public:
    explicit NativePrompt(bool hide);
    ~NativePrompt();
    NativePrompt(const NativePrompt &) = delete;
    NativePrompt &operator=(const NativePrompt &) = delete;
    void restore();

  private:
    std::uint8_t *flags{};
    std::uint8_t visible{};
};
}
