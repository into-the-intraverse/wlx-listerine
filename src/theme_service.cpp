#include "theme_service.h"
#include <toml++/toml.hpp>
#include <windows.h>
#include <functional>

// --- UTF conversion helpers ---

static std::string wstring_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
                        out.data(), len, nullptr, nullptr);
    return out;
}

static std::wstring utf8_to_wstring(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                  nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), len);
    return out;
}

// --- read_palette (file-local) ---

static ColorPalette read_palette(const toml::table& tbl, const ColorPalette& defaults) {
    ColorPalette pal = defaults;
    auto read = [&](const char* key, uint32_t& out) {
        if (auto v = tbl[key].value<std::string>())
            out = ThemeService::parse_hex_color(*v, out);
    };
    read("background",   pal.background);
    read("text",         pal.text);
    read("heading",      pal.heading);
    read("muted",        pal.muted);
    read("link",         pal.link);
    read("link_hover",   pal.link_hover);
    read("code_bg",      pal.code_bg);
    read("quote_border", pal.quote_border);
    read("rule",         pal.rule);
    read("selection",    pal.selection);
    return pal;
}

// --- ThemeService implementation ---

ThemeService::ThemeService()
    : config_(default_config()) {}

ThemeConfig ThemeService::default_config() {
    ThemeConfig cfg;
    // version, detect_string, extensions use struct defaults

    // Light palette (GitHub-inspired)
    cfg.light.background   = 0xFFFFFF;
    cfg.light.text         = 0x1F2328;
    cfg.light.heading      = 0x1F2328;
    cfg.light.muted        = 0x57606A;
    cfg.light.link         = 0x0969DA;
    cfg.light.link_hover   = 0x0550AE;
    cfg.light.code_bg      = 0xF6F8FA;
    cfg.light.quote_border = 0xD0D7DE;
    cfg.light.rule         = 0xD8DEE4;
    cfg.light.selection    = 0xDDEBFF;

    // Dark palette (neutral gray, matches TC dark mode)
    cfg.dark.background   = 0x1E1E1E;
    cfg.dark.text         = 0xD4D4D4;
    cfg.dark.heading      = 0xE0E0E0;
    cfg.dark.muted        = 0x808080;
    cfg.dark.link         = 0x58A6FF;
    cfg.dark.link_hover   = 0x79C0FF;
    cfg.dark.code_bg      = 0x282828;
    cfg.dark.quote_border = 0x404040;
    cfg.dark.rule         = 0x404040;
    cfg.dark.selection    = 0x264F78;

    return cfg;
}

const ColorPalette& ThemeService::palette(bool dark_mode) const {
    return dark_mode ? config_.dark : config_.light;
}

uint32_t ThemeService::parse_hex_color(const std::string& hex, uint32_t fallback) {
    try {
        std::string h = hex;
        if (!h.empty() && h[0] == '#') h = h.substr(1);
        if (h.empty()) return fallback;
        return static_cast<uint32_t>(std::stoul(h, nullptr, 16));
    } catch (...) {
        return fallback;
    }
}

D2D1_COLOR_F ThemeService::to_d2d_color(uint32_t rgb, float alpha) {
    return D2D1::ColorF(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f,
        alpha
    );
}

void ThemeService::load(const std::wstring& toml_path) {
    config_ = default_config();
    try {
        std::string utf8_path = wstring_to_utf8(toml_path);
        auto tbl = toml::parse_file(utf8_path);

        // [general]
        if (auto v = tbl["general"]["detect_string"].value<std::string>())
            config_.detect_string = utf8_to_wstring(*v);

        if (auto arr = tbl["general"]["extensions"].as_array()) {
            config_.extensions.clear();
            for (auto& e : *arr) {
                if (auto s = e.value<std::string>())
                    config_.extensions.push_back(utf8_to_wstring(*s));
            }
        }

        // [fonts]
        if (auto v = tbl["fonts"]["body"].value<std::string>())
            config_.fonts.body_family = utf8_to_wstring(*v);
        if (auto v = tbl["fonts"]["body_size"].value<double>())
            config_.fonts.body_size = static_cast<float>(*v);
        if (auto v = tbl["fonts"]["code"].value<std::string>())
            config_.fonts.code_family = utf8_to_wstring(*v);
        if (auto v = tbl["fonts"]["code_size"].value<double>())
            config_.fonts.code_size = static_cast<float>(*v);
        if (auto v = tbl["fonts"]["emoji"].value<std::string>())
            config_.fonts.emoji_family = utf8_to_wstring(*v);

        // [spacing]
        auto read_spacing = [&](const char* key, float& out) {
            if (auto v = tbl["spacing"][key].value<double>())
                out = static_cast<float>(*v);
        };
        read_spacing("paragraph",            config_.spacing.paragraph_spacing);
        read_spacing("heading_above",        config_.spacing.heading_spacing_above);
        read_spacing("heading_below",        config_.spacing.heading_spacing_below);
        read_spacing("list_indent",          config_.spacing.list_indent);
        read_spacing("quote_indent",         config_.spacing.quote_indent);
        read_spacing("quote_border_width",   config_.spacing.quote_border_width);
        read_spacing("code_padding",         config_.spacing.code_padding);
        read_spacing("line_height_factor",   config_.spacing.line_height_factor);

        // [colors.light] and [colors.dark]
        if (auto lt = tbl["colors"]["light"].as_table())
            config_.light = read_palette(*lt, config_.light);
        if (auto dk = tbl["colors"]["dark"].as_table())
            config_.dark = read_palette(*dk, config_.dark);

        // [code]
        if (auto v = tbl["code"]["grammar_dir"].value<std::string>())
            config_.code_grammar_dir = utf8_to_wstring(*v);
        if (auto v = tbl["code"]["theme_dir"].value<std::string>())
            config_.code_theme_dir = utf8_to_wstring(*v);
        if (auto v = tbl["code"]["default_language"].value<std::string>())
            config_.code_default_language = *v;
        if (auto v = tbl["code"]["theme"].value<std::string>())
            config_.code_theme = *v;

    } catch (...) {
        // Parse failure or missing file -- defaults already set above
    }
}

uint64_t ThemeService::theme_hash() const {
    std::hash<uint64_t> h64;
    std::hash<float> hf;

    // Combine key fields that affect layout/rendering
    uint64_t hash = 0;
    auto combine = [&](uint64_t val) {
        hash ^= h64(val) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    };
    auto combine_f = [&](float val) {
        hash ^= hf(val) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    };

    // Font sizes
    combine_f(config_.fonts.body_size);
    combine_f(config_.fonts.code_size);

    // Spacing
    combine_f(config_.spacing.paragraph_spacing);
    combine_f(config_.spacing.line_height_factor);

    // All light palette colors
    combine(config_.light.background);
    combine(config_.light.text);
    combine(config_.light.heading);
    combine(config_.light.muted);
    combine(config_.light.link);
    combine(config_.light.link_hover);
    combine(config_.light.code_bg);
    combine(config_.light.quote_border);
    combine(config_.light.rule);
    combine(config_.light.selection);

    // All dark palette colors
    combine(config_.dark.background);
    combine(config_.dark.text);
    combine(config_.dark.heading);
    combine(config_.dark.muted);
    combine(config_.dark.link);
    combine(config_.dark.link_hover);
    combine(config_.dark.code_bg);
    combine(config_.dark.quote_border);
    combine(config_.dark.rule);
    combine(config_.dark.selection);

    return hash;
}
