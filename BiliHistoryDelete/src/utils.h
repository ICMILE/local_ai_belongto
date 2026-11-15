#pragma once

#include <nlohmann/json.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

inline std::string FormatTime(long long ts) {
    time_t t = static_cast<time_t>(ts);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return os.str();
}

inline long long GetIntField(const json& item, const char* key) {
    if (!item.contains(key)) return 0;
    const auto& v = item[key];
    if (v.is_number()) return v.get<long long>();
    if (v.is_string()) {
        try { return std::stoll(v.get<std::string>()); } catch (...) { return 0; }
    }
    return 0;
}