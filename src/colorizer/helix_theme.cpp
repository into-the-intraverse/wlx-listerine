#define NOMINMAX
#include "helix_theme.h"
#include "string_util.h"
#include <toml++/toml.hpp>
#include <algorithm>
#include <set>

// ---------------------------------------------------------------------------
// Built-in ANSI / terminal color names (same defaults as Helix)
// ---------------------------------------------------------------------------
static const std::unordered_map<std::string, uint32_t>& ansi_colors() {
    static const std::unordered_map<std::string, uint32_t> map = {
        {"black",         0x000000},
        {"red",           0xCC0000},
        {"green",         0x4E9A06},
        {"yellow",        0xC4A000},
        {"blue",          0x3465A4},
        {"magenta",       0x75507B},
        {"cyan",          0x06989A},
        {"gray",          0xAAAAAA},
        {"light-red",     0xEF2929},
        {"light-green",   0x8AE234},
        {"light-yellow",  0xFCE94F},
        {"light-blue",    0x729FCF},
        {"light-magenta", 0xAD7FA8},
        {"light-cyan",    0x34E2E2},
        {"light-gray",    0xD3D7CF},
        {"white",         0xFFFFFF},
    };
    return map;
}

// ---------------------------------------------------------------------------
// Resolve a color string: palette -> hex -> ANSI name
// ---------------------------------------------------------------------------
static std::optional<uint32_t> resolve_color(
    const std::string& value,
    const std::unordered_map<std::string, uint32_t>& palette)
{
    if (value.empty()) return std::nullopt;

    // 1. Check palette
    auto pit = palette.find(value);
    if (pit != palette.end()) return pit->second;

    // 2. Try hex (#RGB or #RRGGBB)
    if (!value.empty() && value[0] == '#') {
        std::string h = value.substr(1);
        if (h.size() == 3) {
            // Expand #RGB to #RRGGBB
            std::string expanded;
            expanded += h[0]; expanded += h[0];
            expanded += h[1]; expanded += h[1];
            expanded += h[2]; expanded += h[2];
            h = expanded;
        }
        if (h.size() == 6) {
            try {
                return static_cast<uint32_t>(std::stoul(h, nullptr, 16));
            } catch (...) {}
        }
    }

    // 3. Check ANSI names
    auto ait = ansi_colors().find(value);
    if (ait != ansi_colors().end()) return ait->second;

    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Parse a style value (string or table)
// ---------------------------------------------------------------------------
static ResolvedStyle parse_style(
    const toml::node& node,
    const std::unordered_map<std::string, uint32_t>& palette)
{
    ResolvedStyle style;

    if (auto* str = node.as_string()) {
        // Simple string: foreground color only
        auto color = resolve_color(str->get(), palette);
        if (color) {
            style.fg = *color;
            style.has_fg = true;
        }
    } else if (auto* tbl = node.as_table()) {
        // Table form: { fg = "...", bg = "...", modifiers = [...], underline = {...} }
        if (auto fg_val = (*tbl)["fg"].value<std::string>()) {
            auto color = resolve_color(*fg_val, palette);
            if (color) {
                style.fg = *color;
                style.has_fg = true;
            }
        }
        if (auto bg_val = (*tbl)["bg"].value<std::string>()) {
            auto color = resolve_color(*bg_val, palette);
            if (color) {
                style.bg = *color;
                style.has_bg = true;
            }
        }
        if (auto* mods = (*tbl)["modifiers"].as_array()) {
            for (auto&& m : *mods) {
                if (auto* s = m.as_string()) {
                    const std::string& v = s->get();
                    if      (v == "bold")        style.modifiers |= MOD_BOLD;
                    else if (v == "italic")      style.modifiers |= MOD_ITALIC;
                    else if (v == "underlined")  style.modifiers |= MOD_UNDERLINE;
                    else if (v == "crossed_out") style.modifiers |= MOD_STRIKETHROUGH;
                    // others (reversed/dim/blink/hidden) — terminal-only, ignored
                }
            }
        }
        if ((*tbl)["underline"].is_table()) {
            // Helix's table form: underline = { color = "...", style = "..." }.
            // DWrite has only one underline kind, painted in the run's foreground
            // brush, so color and style are intentionally ignored.
            style.modifiers |= MOD_UNDERLINE;
        }
    }

    return style;
}

// ---------------------------------------------------------------------------
// Build the palette map from a [palette] TOML table
// ---------------------------------------------------------------------------
static std::unordered_map<std::string, uint32_t> build_palette(
    const toml::table* palette_tbl)
{
    // Seed with ANSI defaults
    auto palette = ansi_colors();

    if (!palette_tbl) return palette;

    for (auto& [key, val] : *palette_tbl) {
        if (auto* s = val.as_string()) {
            // Palette entries are hex colors or references to other palette names.
            // Try hex first, then existing palette entries.
            std::string color_str = s->get();
            if (!color_str.empty() && color_str[0] == '#') {
                auto c = resolve_color(color_str, {});
                if (c) palette[std::string(key)] = *c;
            } else {
                // Might reference another palette entry
                auto it = palette.find(color_str);
                if (it != palette.end()) {
                    palette[std::string(key)] = it->second;
                }
            }
        }
    }

    return palette;
}

// ---------------------------------------------------------------------------
// Load implementation (recursive for inherits)
// ---------------------------------------------------------------------------
static void load_impl(
    const std::string& theme_name,
    const std::filesystem::path& theme_dir,
    std::unordered_map<std::string, ResolvedStyle>& styles,
    std::set<std::string>& visited)
{
    if (visited.count(theme_name)) return;  // Circular inheritance guard
    visited.insert(theme_name);

    auto path = theme_dir / (theme_name + ".toml");
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (...) {
        return;  // File not found or parse error
    }

    // 1. Handle inheritance first (parent styles loaded first, child overrides)
    if (auto inherits = tbl["inherits"].value<std::string>()) {
        load_impl(*inherits, theme_dir, styles, visited);
    }

    // 2. Build palette (merge parent palette if we had one — but Helix palettes
    //    are per-file, so we just build from this file's [palette])
    auto* palette_tbl = tbl["palette"].as_table();
    auto palette = build_palette(palette_tbl);

    // 3. Parse all style entries (skip reserved keys)
    for (auto& [key, val] : tbl) {
        std::string key_str(key);
        if (key_str == "inherits" || key_str == "palette" || key_str == "rainbow") {
            continue;
        }
        // Skip ui.* scopes
        if (key_str.size() > 3 && key_str[0] == 'u' && key_str[1] == 'i' && key_str[2] == '.') {
            continue;
        }

        auto style = parse_style(val, palette);
        if (style.has_fg || style.has_bg || style.modifiers) {
            styles[key_str] = style;  // Child overrides parent
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

HelixTheme HelixTheme::load(const std::string& theme_name,
                            const std::filesystem::path& theme_dir) {
    HelixTheme theme;
    std::set<std::string> visited;
    load_impl(theme_name, theme_dir, theme.styles_, visited);

    // If loading failed entirely, fall back to built-in dark defaults
    if (theme.styles_.empty()) {
        theme = make_default(true);
    }

    return theme;
}

std::optional<ResolvedStyle> HelixTheme::resolve(const std::string& scope) const {
    if (scope.empty()) return std::nullopt;

    // Try exact match, then strip last dot-segment
    std::string key = scope;
    while (true) {
        auto it = styles_.find(key);
        if (it != styles_.end()) return it->second;

        auto dot = key.rfind('.');
        if (dot == std::string::npos) break;
        key.erase(dot);
    }

    return std::nullopt;
}

uint32_t HelixTheme::resolve_fg(const std::string& scope, uint32_t fallback) const {
    auto style = resolve(scope);
    if (style && style->has_fg) return style->fg;
    return fallback;
}

// ---------------------------------------------------------------------------
// Built-in default theme (VS Code inspired, same colors as before)
// ---------------------------------------------------------------------------
HelixTheme HelixTheme::make_default(bool dark_mode) {
    HelixTheme theme;
    auto& s = theme.styles_;

    auto fg = [](uint32_t color) -> ResolvedStyle {
        return {color, 0, true, false};
    };

    if (!dark_mode) {
        // Light mode (VS Code Light+)
        s["keyword"]            = fg(0xAF00DB);
        s["keyword.directive"]  = fg(0xAF00DB);
        s["keyword.storage"]    = fg(0x0000FF);
        s["conditional"]        = fg(0xAF00DB);
        s["repeat"]             = fg(0xAF00DB);
        s["import"]             = fg(0xAF00DB);
        s["preproc"]            = fg(0xAF00DB);
        s["function"]           = fg(0x795E26);
        s["function.builtin"]   = fg(0x795E26);
        s["function.call"]      = fg(0x795E26);
        s["function.method"]    = fg(0x795E26);
        s["function.special"]   = fg(0x795E26);
        s["method"]             = fg(0x795E26);
        s["method.call"]        = fg(0x795E26);
        s["string"]             = fg(0xA31515);
        s["string.escape"]      = fg(0xEE0000);
        s["string.special"]     = fg(0x811F3F);
        s["string.regexp"]      = fg(0x811F3F);
        s["escape"]             = fg(0xEE0000);
        s["character.special"]  = fg(0xEE0000);
        s["constant"]           = fg(0x001080);
        s["constant.builtin"]   = fg(0x0000FF);
        s["constant.builtin.boolean"] = fg(0x0000FF);
        s["constant.numeric"]   = fg(0x098658);
        s["constant.character"]       = fg(0xA31515);
        s["constant.character.escape"]= fg(0xEE0000);
        s["number"]             = fg(0x098658);
        s["number.float"]       = fg(0x098658);
        s["boolean"]            = fg(0x0000FF);
        s["comment"]            = fg(0x008000);
        s["operator"]           = fg(0x000000);
        s["punctuation"]        = fg(0x000000);
        s["delimiter"]          = fg(0x000000);
        s["type"]               = fg(0x267F99);
        s["type.builtin"]       = fg(0x0000FF);
        s["constructor"]        = fg(0x267F99);
        s["namespace"]          = fg(0x267F99);
        s["module"]             = fg(0x267F99);
        s["variable"]           = fg(0x001080);
        s["variable.builtin"]   = fg(0x0000FF);
        s["variable.parameter"] = fg(0x001080);
        s["variable.other.member"] = fg(0x001080);
        s["parameter"]          = fg(0x001080);
        s["field"]              = fg(0x001080);
        s["tag"]                = fg(0x800000);
        s["tag.builtin"]        = fg(0x800000);
        s["tag.delimiter"]      = fg(0x800000);
        s["attribute"]          = fg(0xFF0000);
        s["property"]           = fg(0x001080);
        s["label"]              = fg(0x001080);
        s["embedded"]           = fg(0xAF00DB);
        s["diagnostic.error"]   = {0, 0xFF4444, false, true};
        s["error"]              = {0, 0xFF4444, false, true};
    } else {
        // Dark mode (VS Code Dark+)
        s["keyword"]            = fg(0xC586C0);
        s["keyword.directive"]  = fg(0xC586C0);
        s["keyword.storage"]    = fg(0x569CD6);
        s["conditional"]        = fg(0xC586C0);
        s["repeat"]             = fg(0xC586C0);
        s["import"]             = fg(0xC586C0);
        s["preproc"]            = fg(0xC586C0);
        s["function"]           = fg(0xDCDCAA);
        s["function.builtin"]   = fg(0xDCDCAA);
        s["function.call"]      = fg(0xDCDCAA);
        s["function.method"]    = fg(0xDCDCAA);
        s["function.special"]   = fg(0xDCDCAA);
        s["method"]             = fg(0xDCDCAA);
        s["method.call"]        = fg(0xDCDCAA);
        s["string"]             = fg(0xCE9178);
        s["string.escape"]      = fg(0xD7BA7D);
        s["string.special"]     = fg(0xD16969);
        s["string.regexp"]      = fg(0xD16969);
        s["escape"]             = fg(0xD7BA7D);
        s["character.special"]  = fg(0xD7BA7D);
        s["constant"]           = fg(0x9CDCFE);
        s["constant.builtin"]   = fg(0x569CD6);
        s["constant.builtin.boolean"] = fg(0x569CD6);
        s["constant.numeric"]   = fg(0xB5CEA8);
        s["constant.character"]       = fg(0xCE9178);
        s["constant.character.escape"]= fg(0xD7BA7D);
        s["number"]             = fg(0xB5CEA8);
        s["number.float"]       = fg(0xB5CEA8);
        s["boolean"]            = fg(0x569CD6);
        s["comment"]            = fg(0x6A9955);
        s["operator"]           = fg(0xD4D4D4);
        s["punctuation"]        = fg(0xD4D4D4);
        s["delimiter"]          = fg(0xD4D4D4);
        s["type"]               = fg(0x4EC9B0);
        s["type.builtin"]       = fg(0x569CD6);
        s["constructor"]        = fg(0x4EC9B0);
        s["namespace"]          = fg(0x4EC9B0);
        s["module"]             = fg(0x4EC9B0);
        s["variable"]           = fg(0x9CDCFE);
        s["variable.builtin"]   = fg(0x569CD6);
        s["variable.parameter"] = fg(0x9CDCFE);
        s["variable.other.member"] = fg(0x9CDCFE);
        s["parameter"]          = fg(0x9CDCFE);
        s["field"]              = fg(0x9CDCFE);
        s["tag"]                = fg(0x569CD6);
        s["tag.builtin"]        = fg(0x569CD6);
        s["tag.delimiter"]      = fg(0x808080);
        s["attribute"]          = fg(0x9CDCFE);
        s["property"]           = fg(0x9CDCFE);
        s["label"]              = fg(0x9CDCFE);
        s["embedded"]           = fg(0xD7BA7D);
        s["diagnostic.error"]   = {0, 0xFF4444, false, true};
        s["error"]              = {0, 0xFF4444, false, true};
    }

    return theme;
}
