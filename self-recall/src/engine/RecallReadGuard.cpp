#include "totk/engine/ReadGuard.hpp"

#include <lib.hpp>

#include <array>
#include <atomic>

namespace totk::engine::read_guard {
namespace {
std::array<std::atomic<std::uintptr_t>, 32> g_sites{};
std::atomic<std::uint64_t> g_refusals{};
}

void reject(std::uintptr_t, std::size_t size) {
    const auto site = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    const auto total = g_refusals.fetch_add(1, std::memory_order_relaxed) + 1;
    for (auto& slot : g_sites) {
        auto seen = slot.load(std::memory_order_relaxed);
        if (seen == site) return;
        if (seen || !slot.compare_exchange_strong(seen, site, std::memory_order_relaxed)) {
            if (seen == site) return;
            continue;
        }
        // Add read_guard::reject's link address to site for addr2line.
        Logging.Log("[self-recall] READ_REFUSED site=%lx size=%zu total=%llu",
                    site - reinterpret_cast<std::uintptr_t>(&reject), size,
                    static_cast<unsigned long long>(total));
        return;
    }
}

}
