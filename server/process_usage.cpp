#include "process_usage.h"

#ifdef __linux__
#include <unistd.h>

#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#endif

ProcessUsage currentProcessUsage() {
    ProcessUsage usage;
#ifdef __linux__
    // /proc/self/stat: field 14 is utime, 15 stime (clock ticks), 24 rss (pages). The second field (the name, in
    // parentheses) may hold spaces, so fields are counted from after its closing parenthesis.
    std::ifstream file("/proc/self/stat");
    const std::string stat((std::istreambuf_iterator<char>(file)), {});
    const auto close = stat.rfind(')');
    if (close == std::string::npos || close + 2 > stat.size()) return usage;
    std::istringstream fields(stat.substr(close + 2));  // starts at field 3
    std::string field;
    long long utime = 0, stime = 0, rss = 0;
    for (int i = 3; i <= 24 && fields >> field; ++i) {
        if (i == 14) utime = std::stoll(field);
        if (i == 15) stime = std::stoll(field);
        if (i == 24) rss = std::stoll(field);
    }
    usage.cpuSeconds = static_cast<double>(utime + stime) / static_cast<double>(sysconf(_SC_CLK_TCK));
    usage.rssBytes = rss * sysconf(_SC_PAGESIZE);
#endif
    return usage;
}
