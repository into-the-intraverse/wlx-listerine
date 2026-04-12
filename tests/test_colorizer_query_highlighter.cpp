#include <doctest/doctest.h>
#include <filesystem>
#include "query_highlighter.h"
#include "grammar_registry.h"

// ---------------------------------------------------------------------------
// Null-safety tests
// ---------------------------------------------------------------------------

TEST_CASE("QueryHighlighter: null tree returns empty") {
    SyntaxPalette pal = SyntaxPalette::defaults(false);
    // Need a valid query for this test — but tree is null
    // Pass nullptr for both; should still be empty
    auto spans = QueryHighlighter::highlight(nullptr, nullptr, pal, "");
    CHECK(spans.empty());
}

TEST_CASE("QueryHighlighter: null query returns empty") {
    SyntaxPalette pal = SyntaxPalette::defaults(false);
    // We need a real tree but null query
    if (!std::filesystem::exists("grammars/c/tree-sitter-c.dll")) {
        // Can't make a tree without a grammar; just test nullptr/nullptr
        auto spans = QueryHighlighter::highlight(nullptr, nullptr, pal, "");
        CHECK(spans.empty());
        return;
    }
    GrammarRegistry reg(L"grammars");
    auto* tree = reg.parse("c", "int x = 1;");
    REQUIRE(tree != nullptr);
    auto spans = QueryHighlighter::highlight(tree, nullptr, pal, "int x = 1;");
    CHECK(spans.empty());
    ts_tree_delete(tree);
}

// ---------------------------------------------------------------------------
// Helper: verify non-overlapping
// ---------------------------------------------------------------------------
static void check_non_overlapping(const std::vector<ColorSpan>& spans) {
    for (size_t i = 1; i < spans.size(); i++) {
        INFO("span[" << i-1 << "] start=" << spans[i-1].start
             << " length=" << spans[i-1].length
             << "  span[" << i << "] start=" << spans[i].start);
        CHECK(spans[i].start >= spans[i-1].start + spans[i-1].length);
    }
}

// ---------------------------------------------------------------------------
// C grammar tests
// ---------------------------------------------------------------------------

static bool has_c_grammar() {
    return std::filesystem::exists("grammars/c/tree-sitter-c.dll");
}

TEST_CASE("QueryHighlighter: C code produces spans"
    * doctest::skip(!has_c_grammar())) {
    GrammarRegistry reg(L"grammars");
    SyntaxPalette pal = SyntaxPalette::defaults(false);

    const char* source = "int main() { return 0; }";
    auto* tree = reg.parse("c", source);
    auto* query = reg.get_query("c");
    REQUIRE(tree != nullptr);
    REQUIRE(query != nullptr);

    auto spans = QueryHighlighter::highlight(tree, query, pal, source);
    CHECK(!spans.empty());
    check_non_overlapping(spans);

    // "int" should be highlighted (keyword or type)
    bool found_int = false;
    for (const auto& s : spans) {
        if (s.start == 0 && s.length == 3) {
            found_int = true;
            break;
        }
    }
    CHECK(found_int);

    ts_tree_delete(tree);
}

// ---------------------------------------------------------------------------
// JSON grammar tests
// ---------------------------------------------------------------------------

static bool has_json_grammar() {
    return std::filesystem::exists("grammars/json/tree-sitter-json.dll");
}

TEST_CASE("QueryHighlighter: JSON produces spans"
    * doctest::skip(!has_json_grammar())) {
    GrammarRegistry reg(L"grammars");
    SyntaxPalette pal = SyntaxPalette::defaults(false);

    const char* source = R"({"name": "test", "value": 42, "flag": true})";
    auto* tree = reg.parse("json", source);
    auto* query = reg.get_query("json");
    REQUIRE(tree != nullptr);
    REQUIRE(query != nullptr);

    auto spans = QueryHighlighter::highlight(tree, query, pal, source);
    CHECK(!spans.empty());
    check_non_overlapping(spans);

    ts_tree_delete(tree);
}

// ---------------------------------------------------------------------------
// Python grammar tests
// ---------------------------------------------------------------------------

static bool has_python_grammar() {
    return std::filesystem::exists("grammars/python/tree-sitter-python.dll");
}

TEST_CASE("QueryHighlighter: Python produces spans"
    * doctest::skip(!has_python_grammar())) {
    GrammarRegistry reg(L"grammars");
    SyntaxPalette pal = SyntaxPalette::defaults(false);

    const char* source = "def hello():\n    print(\"world\")\n";
    auto* tree = reg.parse("python", source);
    auto* query = reg.get_query("python");
    REQUIRE(tree != nullptr);
    REQUIRE(query != nullptr);

    auto spans = QueryHighlighter::highlight(tree, query, pal, source);
    CHECK(!spans.empty());
    check_non_overlapping(spans);

    // "def" should be highlighted as keyword
    bool found_def = false;
    for (const auto& s : spans) {
        if (s.start == 0 && s.length == 3) {
            found_def = true;
            break;
        }
    }
    CHECK(found_def);

    ts_tree_delete(tree);
}

// ---------------------------------------------------------------------------
// Non-overlapping property (explicit)
// ---------------------------------------------------------------------------

TEST_CASE("QueryHighlighter: spans are non-overlapping for C"
    * doctest::skip(!has_c_grammar())) {
    GrammarRegistry reg(L"grammars");
    SyntaxPalette pal = SyntaxPalette::defaults(false);

    // Longer sample with overlapping potential
    const char* source =
        "#include <stdio.h>\n"
        "int main(int argc, char** argv) {\n"
        "    printf(\"hello %s\\n\", argv[1]);\n"
        "    return 0;\n"
        "}\n";

    auto* tree = reg.parse("c", source);
    auto* query = reg.get_query("c");
    REQUIRE(tree != nullptr);
    REQUIRE(query != nullptr);

    auto spans = QueryHighlighter::highlight(tree, query, pal, source);
    CHECK(!spans.empty());
    check_non_overlapping(spans);

    ts_tree_delete(tree);
}

// ---------------------------------------------------------------------------
// Dark mode produces different colors than light mode
// ---------------------------------------------------------------------------

TEST_CASE("QueryHighlighter: dark mode produces different colors"
    * doctest::skip(!has_c_grammar())) {
    GrammarRegistry reg(L"grammars");
    SyntaxPalette light_pal = SyntaxPalette::defaults(false);
    SyntaxPalette dark_pal = SyntaxPalette::defaults(true);

    const char* source = "int main() { return 0; }";
    auto* tree = reg.parse("c", source);
    auto* query = reg.get_query("c");
    REQUIRE(tree != nullptr);
    REQUIRE(query != nullptr);

    auto light_spans = QueryHighlighter::highlight(tree, query, light_pal, source);
    auto dark_spans = QueryHighlighter::highlight(tree, query, dark_pal, source);

    // Same number of spans (same structure, different colors)
    REQUIRE(light_spans.size() == dark_spans.size());
    CHECK(!light_spans.empty());

    // At least one span has a different color
    bool found_diff = false;
    for (size_t i = 0; i < light_spans.size(); i++) {
        CHECK(light_spans[i].start == dark_spans[i].start);
        CHECK(light_spans[i].length == dark_spans[i].length);
        if (light_spans[i].color != dark_spans[i].color) {
            found_diff = true;
        }
    }
    CHECK(found_diff);

    ts_tree_delete(tree);
}
