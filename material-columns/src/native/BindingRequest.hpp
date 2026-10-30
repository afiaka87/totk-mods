// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstddef>
#include <cstdint>

namespace material_columns::native {
// SlideListPage::onDisplay_ builds this request; onActive_ forwards it in X3.
// It is not the page object, although both have a controller pointer at +8.
struct BindingRequest {
    std::int32_t logical{};
    std::int32_t offset{};
    std::uintptr_t controller{};
    std::uintptr_t page{};
    std::uintptr_t pane{};
};
static_assert(sizeof(BindingRequest) == 32);
static_assert(offsetof(BindingRequest, controller) == 8);
static_assert(offsetof(BindingRequest, page) == 16);
static_assert(offsetof(BindingRequest, pane) == 24);
}
