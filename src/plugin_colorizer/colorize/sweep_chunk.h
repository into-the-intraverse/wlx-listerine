#pragma once

#include "runtime/host/async_loader.h"

#include <algorithm>
#include <cstdint>

namespace wlx::plugin_colorizer::colorize {

// Adaptive sweep chunk sizing. Each chunk's highlight holds the process-wide
// core mutex; a fixed 256 KB chunk would hold it ~2 s on pathological inputs
// (template-heavy C++ runs ~7 ms/KB), stalling concurrent viewport
// highlights. Target ~25 ms per chunk instead, learned from the previous
// chunk's measured cost.
inline constexpr uint32_t kSweepFirstChunkBytes = 64 * 1024;
inline constexpr uint32_t kSweepMinChunkBytes   = 16 * 1024;
inline constexpr uint32_t kSweepMaxChunkBytes   = 1024 * 1024;

inline uint32_t next_chunk_bytes(uint32_t prev_bytes, double prev_ms) {
    constexpr double kTargetMs = 25.0;
    if (prev_ms <= 0.01) return kSweepMaxChunkBytes;
    const double scaled = static_cast<double>(prev_bytes) * (kTargetMs / prev_ms);
    return static_cast<uint32_t>(std::clamp(
        scaled, static_cast<double>(kSweepMinChunkBytes),
        static_cast<double>(kSweepMaxChunkBytes)));
}

// True when an in-flight sweep must vanish at the next chunk edge: the view
// was superseded (generation bumped by reload/relang/dark-flip), closed, or
// the module is detaching.
inline bool sweep_superseded(const wlx::runtime::host::ViewLiveToken& live,
                             uint64_t my_generation) {
    return wlx::runtime::host::g_shutting_down.load(std::memory_order_acquire) ||
           live.closed.load(std::memory_order_acquire) ||
           live.generation.load(std::memory_order_acquire) != my_generation;
}

}  // namespace wlx::plugin_colorizer::colorize
