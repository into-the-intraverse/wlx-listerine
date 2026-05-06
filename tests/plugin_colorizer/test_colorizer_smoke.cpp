#include <doctest/doctest.h>

#include "core_dll/colorizer/colorizer.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using wlx::core::colorizer::Colorizer;
namespace fs = std::filesystem;

static const bool grammars_present = fs::exists("grammars/cpp/tree-sitter-cpp.dll");
static const bool sample_present   = fs::exists("test_data/grammar_samples/sample.cpp");

namespace {

// Read a file as raw bytes (UTF-8). The bundled sample.cpp is ASCII, so no
// BOM/encoding handling is needed for this smoke — we deliberately skip
// FileService to avoid pulling wlx-core into colorizer-tests.
std::string slurp_utf8(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("smoke: colorize sample.cpp returns colored spans"
    * doctest::skip(!grammars_present || !sample_present)) {

    Colorizer c(L"grammars", L"config/themes");
    REQUIRE(c.supports("cpp"));

    std::string source = slurp_utf8("test_data/grammar_samples/sample.cpp");
    REQUIRE_FALSE(source.empty());

    auto result = c.colorize(source, "cpp", /*dark_mode=*/true);

    // Smoke threshold: catches "wiring is dead" (zero/near-zero spans) without
    // tracking the precise span count. Today's sample.cpp produces 100 spans;
    // 30 leaves generous headroom so query churn doesn't flake the smoke.
    CHECK(result.spans.size() >= 30);

    // At least one span must have a non-zero color (otherwise the theme
    // didn't resolve anything → silent breakage).
    bool any_colored = false;
    for (const auto& s : result.spans) {
        if (s.color != 0) { any_colored = true; break; }
    }
    CHECK(any_colored);
}
