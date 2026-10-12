#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace pinx::app {

inline std::string HumanSize(std::uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while(value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }
    char buf[32];
    if(unit == 0)
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    else
        std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    return buf;
}

}
