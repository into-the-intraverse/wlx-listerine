#include "core_config.h"
#include <toml++/toml.hpp>
#include <filesystem>

CoreConfig CoreConfig::load(const std::wstring& core_dir) {
    CoreConfig cfg;
    std::filesystem::path path = std::filesystem::path(core_dir) / "wlx-listerine-core.toml";

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return cfg;

    try {
        auto tbl = toml::parse_file(path.string());

        if (auto v = tbl["grammar_cache"]["cap"].value<int64_t>()) {
            int64_t n = *v;
            // Allow 1..1000; anything outside falls back to default 8.
            cfg.cap = (n >= 1 && n <= 1000) ? static_cast<uint32_t>(n) : 8u;
        }
        if (auto v = tbl["grammar_cache"]["ttl_minutes"].value<int64_t>()) {
            int64_t n = *v;
            // Allow 1..1440 (1 day); else fall back.
            cfg.ttl_minutes = (n >= 1 && n <= 1440) ? static_cast<uint32_t>(n) : 5u;
        }
        if (auto v = tbl["theme"]["dark"].value<std::string>()) {
            if (!v->empty()) cfg.theme = *v;
        }
        if (auto v = tbl["theme"]["light"].value<std::string>()) {
            cfg.theme_light = *v;
        }
    } catch (...) {
        // Bad TOML: defaults already applied.
    }
    return cfg;
}
