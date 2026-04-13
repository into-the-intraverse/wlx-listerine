#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "helix_theme.h"

struct ColorSpan {
    uint32_t start = 0;   // byte offset in UTF-8 source
    uint32_t length = 0;
    uint32_t color = 0;   // foreground 0x00RRGGBB
    uint32_t bg_color = 0;
    bool has_bg = false;
};

struct ColorizeResult {
    std::vector<ColorSpan> spans;  // sorted by start, non-overlapping
};

class GrammarRegistry;

class Colorizer {
public:
    // theme_dir: directory containing Helix-format .toml theme files
    // theme_name: default theme (used for dark mode, or both modes)
    // theme_light_name: optional light-mode override (empty = use theme_name)
    Colorizer(const std::wstring& grammar_dir,
              const std::wstring& theme_dir,
              const std::string& theme_name = "default",
              const std::string& theme_light_name = "");
    ~Colorizer();

    ColorizeResult colorize(const std::string& source,
                            const std::string& language,
                            bool dark_mode);

    bool supports(const std::string& language) const;
    std::vector<std::string> available_languages() const;

    const HelixTheme& theme(bool dark_mode) const;

private:
    std::unique_ptr<GrammarRegistry> grammar_registry_;
    HelixTheme dark_theme_;
    HelixTheme light_theme_;
};
