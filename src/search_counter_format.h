#pragma once

#include <string>

inline std::wstring format_counter(int current_one_based, int total) {
    return std::to_wstring(current_one_based) + L" / " + std::to_wstring(total);
}
