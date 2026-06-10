#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <charconv>
#include <string>
#include <string_view>

namespace wlx::runtime::util {


inline std::string wstring_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
                        out.data(), len, nullptr, nullptr);
    return out;
}

inline std::wstring utf8_to_wstring(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                  nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), len);
    return out;
}

// Strict "#RRGGBB" / "#RGB" parser ('#' optional). 3-digit shorthand expands
// per CSS ("abc" -> 0xAABBCC). Wrong length, non-hex characters, or signs
// return the fallback. Result is always 24-bit RGB — the renderer's brush
// cache keys alpha into the high byte, so it must stay clear.
inline uint32_t parse_hex_color(const std::string& hex, uint32_t fallback = 0) {
    std::string_view h = hex;
    if (!h.empty() && h[0] == '#') h.remove_prefix(1);
    char expanded[6];
    if (h.size() == 3) {
        for (int i = 0; i < 3; i++) expanded[2 * i] = expanded[2 * i + 1] = h[i];
        h = std::string_view(expanded, 6);
    }
    if (h.size() != 6) return fallback;
    uint32_t value = 0;
    auto [end, ec] = std::from_chars(h.data(), h.data() + 6, value, 16);
    if (ec != std::errc{} || end != h.data() + 6) return fallback;
    return value & 0xFFFFFF;
}

}  // namespace wlx::runtime::util
