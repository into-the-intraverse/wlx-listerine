#include <doctest/doctest.h>
#include "core_config.h"
#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static std::wstring make_temp_toml(const std::string& body) {
    auto dir = fs::temp_directory_path() / "wlx_core_cfg_test";
    fs::create_directories(dir);
    auto p = dir / "wlx-listerine-core.toml";
    std::ofstream(p) << body;
    return p.parent_path().wstring() + L"\\";
}

TEST_CASE("CoreConfig returns defaults when file missing") {
    auto cfg = CoreConfig::load(L"definitely_does_not_exist\\");
    CHECK(cfg.cap == 8);
    CHECK(cfg.ttl_minutes == 5);
}

TEST_CASE("CoreConfig parses valid values") {
    auto dir = make_temp_toml(
        "[grammar_cache]\ncap = 16\nttl_minutes = 10\n");
    auto cfg = CoreConfig::load(dir);
    CHECK(cfg.cap == 16);
    CHECK(cfg.ttl_minutes == 10);
}

TEST_CASE("CoreConfig clamps out-of-range cap") {
    auto dir = make_temp_toml(
        "[grammar_cache]\ncap = 0\nttl_minutes = 5\n");
    auto cfg = CoreConfig::load(dir);
    CHECK(cfg.cap == 8);  // clamped to default
}

TEST_CASE("CoreConfig clamps absurd ttl") {
    auto dir = make_temp_toml(
        "[grammar_cache]\ncap = 8\nttl_minutes = -3\n");
    auto cfg = CoreConfig::load(dir);
    CHECK(cfg.ttl_minutes == 5);  // clamped to default
}

TEST_CASE("CoreConfig falls back on parse failure") {
    auto dir = make_temp_toml("this is not :: valid TOML\n");
    auto cfg = CoreConfig::load(dir);
    CHECK(cfg.cap == 8);
    CHECK(cfg.ttl_minutes == 5);
}

TEST_CASE("CoreConfig parses theme names") {
    auto dir = make_temp_toml(
        "[theme]\ndark = \"my-theme\"\nlight = \"my-theme-light\"\n");
    auto cfg = CoreConfig::load(dir);
    CHECK(cfg.theme == "my-theme");
    CHECK(cfg.theme_light == "my-theme-light");
}

TEST_CASE("CoreConfig defaults theme when missing") {
    auto cfg = CoreConfig::load(L"definitely_does_not_exist\\");
    CHECK(cfg.theme == "default");
    CHECK(cfg.theme_light == "");
}
