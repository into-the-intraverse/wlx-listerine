#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

struct SyntaxPalette {
    uint32_t keyword = 0;
    uint32_t keyword2 = 0;
    uint32_t function = 0;
    uint32_t string = 0;
    uint32_t number = 0;
    uint32_t comment = 0;
    uint32_t op = 0;       // "operator" is a C++ keyword
    uint32_t type = 0;
    uint32_t preprocessor = 0;
    uint32_t ns = 0;       // "namespace" is a C++ keyword
    uint32_t variable = 0;
    uint32_t punctuation = 0;
    uint32_t plain = 0;

    static SyntaxPalette defaults(bool dark_mode);
};

class ThemeLoader {
public:
    explicit ThemeLoader(const std::wstring& theme_dir);

    SyntaxPalette palette_for(const std::string& language, bool dark_mode) const;
    void set_language_theme(const std::string& language, const std::string& theme_name);

private:
    void load_theme_file(const std::string& theme_name) const;

    std::wstring theme_dir_;
    std::unordered_map<std::string, std::string> language_to_theme_;

    struct ThemeData {
        SyntaxPalette light;
        SyntaxPalette dark;
    };
    mutable std::unordered_map<std::string, ThemeData> loaded_themes_;
};
