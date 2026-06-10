#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <cstddef>

namespace wlx::tools::screenshot {

// One GetProcessMemoryInfo fetch covering both numbers --bench reports:
// the current working set (for the delta vs. baseline) and the process
// peak (high-water mark). Shared by the markdown and colorizer pipelines.
struct WorkingSetSample {
    size_t current = 0;
    size_t peak    = 0;
};

inline WorkingSetSample sample_working_set() {
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return { pmc.WorkingSetSize, pmc.PeakWorkingSetSize };
}

}  // namespace wlx::tools::screenshot
