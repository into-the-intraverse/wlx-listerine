#pragma once

#include "runtime/host/async_loader.h"

#include <algorithm>
#include <cstdint>

namespace wlx::plugin_colorizer::colorize {

// Adaptive sweep chunk sizing. Each chunk's highlight holds the process-wide
// core mutex; query cost density varies WILDLY within one file (json.hpp:
// 64 KB of header comments at ~0.05 ms/KB, then template code at ~8 ms/KB),
// so throughput learned on a cheap region must never size a chunk that lands
// in an expensive one — the old 1 MB cap let one chunk balloon to 485 KB and
// hold the mutex for ~4 s. Target ~25 ms per chunk, grow at most 2x per step,
// and cap at 64 KB (worst observed density caps a hold at ~0.5 s; the
// viewport painter additionally skips past in-flight chunks rather than
// blocking, so even that bound is off the UI's critical path).
inline constexpr uint32_t kSweepFirstChunkBytes = 64 * 1024;
inline constexpr uint32_t kSweepMinChunkBytes   = 16 * 1024;
inline constexpr uint32_t kSweepMaxChunkBytes   = 64 * 1024;

inline uint32_t next_chunk_bytes(uint32_t prev_bytes, double prev_ms) {
    constexpr double kTargetMs = 25.0;
    const uint32_t growth_cap =
        std::min(prev_bytes > kSweepMaxChunkBytes / 2 ? kSweepMaxChunkBytes
                                                      : prev_bytes * 2,
                 kSweepMaxChunkBytes);
    if (prev_ms <= 0.01) return growth_cap;
    const double scaled = static_cast<double>(prev_bytes) * (kTargetMs / prev_ms);
    return static_cast<uint32_t>(std::clamp(
        scaled, static_cast<double>(kSweepMinChunkBytes),
        static_cast<double>(growth_cap)));
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
