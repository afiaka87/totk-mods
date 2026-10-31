// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include <rtld.hpp>
namespace nn::ro::detail {
// 1.4.2 exports auto-loaded modules but no optional manual-module lookup callback.
[[gnu::visibility("hidden")]] rtld::lookup_global_t g_LookupGlobalManualFunctionPointer = nullptr;
}
