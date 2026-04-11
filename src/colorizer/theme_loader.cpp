#define NOMINMAX
#include "theme_loader.h"
#include "string_util.h"
#include <toml++/toml.hpp>

// --- SyntaxPalette::defaults ---

SyntaxPalette SyntaxPalette::defaults(bool dark_mode) {
    SyntaxPalette pal;
    if (!dark_mode) {
        pal.keyword     = 0xAF00DB;
        pal.keyword2    = 0x0000FF;
        pal.function    = 0x795E26;
        pal.string      = 0xA31515;
        pal.number      = 0x098658;
        pal.comment     = 0x008000;
        pal.op          = 0x000000;
        pal.type        = 0x267F99;
        pal.preprocessor = 0xAF00DB;
        pal.ns          = 0x267F99;
        pal.variable    = 0x001080;
        pal.punctuation = 0x000000;
        pal.plain       = 0x1F2328;
    } else {
        pal.keyword     = 0xC586C0;
        pal.keyword2    = 0x569CD6;
        pal.function    = 0xDCDCAA;
        pal.string      = 0xCE9178;
        pal.number      = 0xB5CEA8;
        pal.comment     = 0x6A9955;
        pal.op          = 0xD4D4D4;
        pal.type        = 0x4EC9B0;
        pal.preprocessor = 0xC586C0;
        pal.ns          = 0x4EC9B0;
        pal.variable    = 0x9CDCFE;
        pal.punctuation = 0xD4D4D4;
        pal.plain       = 0xD4D4D4;
    }
    return pal;
}

// --- ThemeLoader implementation ---

ThemeLoader::ThemeLoader(const std::wstring& theme_dir)
    : theme_dir_(theme_dir) {}

void ThemeLoader::load_theme_file(const std::string& theme_name) const {
    if (loaded_themes_.count(theme_name)) return;

    // Build path: {theme_dir}/{theme_name}.toml
    std::wstring path = theme_dir_ + L"/" + utf8_to_wstring(theme_name) + L".toml";
    std::string utf8_path = wstring_to_utf8(path);

    ThemeData data;
    data.light = SyntaxPalette::defaults(false);
    data.dark  = SyntaxPalette::defaults(true);

    try {
        auto tbl = toml::parse_file(utf8_path);

        auto read_palette = [&](const char* section, SyntaxPalette& pal) {
            auto st = tbl[section].as_table();
            if (!st) return;
            auto read = [&](const char* key, uint32_t& out) {
                if (auto v = (*st)[key].value<std::string>())
                    out = parse_hex_color(*v, out);
            };
            read("keyword",     pal.keyword);
            read("keyword2",    pal.keyword2);
            read("function",    pal.function);
            read("string",      pal.string);
            read("number",      pal.number);
            read("comment",     pal.comment);
            read("operator",    pal.op);
            read("type",        pal.type);
            read("preprocessor", pal.preprocessor);
            read("namespace",   pal.ns);
            read("variable",    pal.variable);
            read("punctuation", pal.punctuation);
            read("plain",       pal.plain);
        };

        read_palette("light", data.light);
        read_palette("dark",  data.dark);

    } catch (...) {
        // File not found or parse failure -- keep defaults already set
    }

    loaded_themes_[theme_name] = data;
}

SyntaxPalette ThemeLoader::palette_for(const std::string& language, bool dark_mode) const {
    auto it = language_to_theme_.find(language);
    const std::string& theme_name = (it != language_to_theme_.end()) ? it->second : "default";

    load_theme_file(theme_name);

    auto td = loaded_themes_.find(theme_name);
    if (td == loaded_themes_.end()) {
        return SyntaxPalette::defaults(dark_mode);
    }
    return dark_mode ? td->second.dark : td->second.light;
}

void ThemeLoader::set_language_theme(const std::string& language, const std::string& theme_name) {
    language_to_theme_[language] = theme_name;
}
