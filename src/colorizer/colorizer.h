#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

struct ColorSpan {
    uint32_t start = 0;   // byte offset in UTF-8 source
    uint32_t length = 0;
    uint32_t color = 0;   // 0x00RRGGBB
};

struct ColorizeResult {
    std::vector<ColorSpan> spans;  // sorted by start, non-overlapping
};

class GrammarRegistry;
class ThemeLoader;

class Colorizer {
public:
    Colorizer(const std::wstring& grammar_dir, const std::wstring& theme_dir);
    ~Colorizer();

    ColorizeResult colorize(const std::string& source,
                            const std::string& language,
                            bool dark_mode) const;

    bool supports(const std::string& language) const;
    std::vector<std::string> available_languages() const;

    void set_language_theme(const std::string& language, const std::string& theme_name);
    ThemeLoader& theme_loader() { return *theme_loader_; }

private:
    std::unique_ptr<GrammarRegistry> grammar_registry_;
    std::unique_ptr<ThemeLoader> theme_loader_;
};
