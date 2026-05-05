#pragma once

#include <string>

namespace wlx::runtime::theme {


struct FontConfig {
    std::wstring body_family = L"Segoe UI";
    std::wstring code_family = L"Cascadia Code";
    std::wstring emoji_family = L"Segoe UI Emoji";
    float body_size = 14.0f;
    float code_size = 13.0f;
};

}  // namespace wlx::runtime::theme
