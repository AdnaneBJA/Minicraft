#pragma once

#include <cstdint>

// The server process's own memory and CPU, for the health it reports: read from /proc on Linux (production), zero
// elsewhere.
struct ProcessUsage {
    std::int64_t rssBytes = 0;  // resident memory
    double cpuSeconds = 0;      // user + system CPU time since the process started
};

ProcessUsage currentProcessUsage();
