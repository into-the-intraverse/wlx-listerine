#include "config.h"
#include <toml++/toml.hpp>
#include <fstream>

uint32_t parse_hex_color(const std::string& hex, uint32_t fallback) {
    try {
        std::string h = hex;
        if (!h.empty() && h[0] == '#') h = h.substr(1);
        return static_cast<uint32_t>(std::stoul(h, nullptr, 16));
    } catch (...) {
        return fallback;
    }
}

uint32_t parse_hex_color(const std::string& hex) {
    return parse_hex_color(hex, 0);
}

Config default_config() {
    Config cfg;
    cfg.extensions = {"md", "markdown", "mdown", "mkd", "mkdn"};
    cfg.detect_string = R"(EXT="MD" | EXT="MARKDOWN")";
    cfg.body_font = "Segoe UI";
    cfg.body_size = 11;
    cfg.code_font = "Consolas";
    cfg.code_size = 10;

    cfg.light = {0xFFFFFF, 0x24292E, 0x24292E, 0xF6F8FA, 0x6A737D, 0x0366D6};
    cfg.dark  = {0x1E1E1E, 0xD4D4D4, 0xE0E0E0, 0x2D2D2D, 0x9E9E9E, 0x58A6FF};

    return cfg;
}

static ColorScheme read_colors(const toml::table& tbl, const ColorScheme& defaults) {
    ColorScheme cs = defaults;
    auto c = [&](const char* key, uint32_t& out) {
        if (auto v = tbl[key].value<std::string>()) out = parse_hex_color(*v, out);
    };
    c("background", cs.background);
    c("text", cs.text);
    c("heading", cs.heading);
    c("code_background", cs.code_background);
    c("blockquote", cs.blockquote);
    c("link", cs.link);
    return cs;
}

Config load_config(const std::string& path) {
    Config cfg = default_config();
    try {
        auto tbl = toml::parse_file(path);

        if (auto arr = tbl["options"]["extensions"].as_array()) {
            cfg.extensions.clear();
            for (auto& e : *arr)
                if (auto s = e.value<std::string>()) cfg.extensions.push_back(*s);
        }
        if (auto v = tbl["options"]["detect_string"].value<std::string>())
            cfg.detect_string = *v;

        if (auto v = tbl["fonts"]["body"].value<std::string>()) cfg.body_font = *v;
        if (auto v = tbl["fonts"]["body_size"].value<int64_t>()) cfg.body_size = static_cast<int>(*v);
        if (auto v = tbl["fonts"]["code"].value<std::string>()) cfg.code_font = *v;
        if (auto v = tbl["fonts"]["code_size"].value<int64_t>()) cfg.code_size = static_cast<int>(*v);

        if (auto lt = tbl["colors"]["light"].as_table()) cfg.light = read_colors(*lt, cfg.light);
        if (auto dk = tbl["colors"]["dark"].as_table())  cfg.dark  = read_colors(*dk, cfg.dark);
    } catch (...) {
        // Missing or malformed file — defaults are fine
    }
    return cfg;
}
