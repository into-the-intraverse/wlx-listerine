#include <doctest/doctest.h>
#include "runtime/interaction/url_scanner.h"

#include <string>

using namespace wlx::runtime::interaction;

TEST_CASE("scan_urls - empty input") {
    auto matches = scan_urls(L"");
    CHECK(matches.empty());
}

TEST_CASE("scan_urls - bare https URL") {
    std::wstring text = L"https://example.com/path";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(m[0].start == 0);
    CHECK(m[0].end == static_cast<int>(text.size()));
}

TEST_CASE("scan_urls - URL inside surrounding text") {
    std::wstring text = L"See https://example.com/page for details.";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(m[0].start == 4);
    CHECK(m[0].end == 28);  // up to '/page' end, before space
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"https://example.com/page");
}

TEST_CASE("scan_urls - trailing period is trimmed") {
    std::wstring text = L"go to https://example.com/page.";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"https://example.com/page");
}

TEST_CASE("scan_urls - trailing close-paren is trimmed") {
    std::wstring text = L"(see https://example.com/page)";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"https://example.com/page");
}

TEST_CASE("scan_urls - trailing close-bracket and exclamation are trimmed") {
    std::wstring text = L"[https://example.com/!]";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"https://example.com/");
}

TEST_CASE("scan_urls - URL with port and query string") {
    std::wstring text = L"  https://api.example.com:8080/v1/items?id=42&limit=10  ";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) ==
          L"https://api.example.com:8080/v1/items?id=42&limit=10");
}

TEST_CASE("scan_urls - URL inside an identifier is rejected") {
    std::wstring text = L"parsehttp://x.org/y";
    auto m = scan_urls(text);
    CHECK(m.empty());
}

TEST_CASE("scan_urls - http and https side by side") {
    std::wstring text = L"a http://x.org/ b https://y.org/ c";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 2);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"http://x.org/");
    CHECK(text.substr(m[1].start, m[1].end - m[1].start) == L"https://y.org/");
}

TEST_CASE("scan_urls - file:// scheme") {
    std::wstring text = L"local: file:///C:/tmp/x.txt yes";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"file:///C:/tmp/x.txt");
}

TEST_CASE("scan_urls - ftp:// scheme") {
    std::wstring text = L"ftp://files.example.com/archive.zip";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(m[0].start == 0);
    CHECK(m[0].end == static_cast<int>(text.size()));
}

TEST_CASE("scan_urls - URL inside a JSON string literal") {
    std::wstring text = LR"(    "url": "https://files.pythonhosted.org/pkg/x.whl",)";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) ==
          L"https://files.pythonhosted.org/pkg/x.whl");
}

TEST_CASE("scan_urls - URL inside a C-style comment") {
    std::wstring text = L"// see https://docs.example.com for usage";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) ==
          L"https://docs.example.com");
}

TEST_CASE("scan_urls - case-insensitive scheme") {
    std::wstring text = L"HTTPS://EXAMPLE.COM/PATH";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(m[0].start == 0);
    CHECK(m[0].end == static_cast<int>(text.size()));
}

TEST_CASE("scan_urls - scheme alone is not a match") {
    std::wstring text = L"https:// alone";
    auto m = scan_urls(text);
    CHECK(m.empty());
}

TEST_CASE("scan_urls - line with no URL produces nothing") {
    std::wstring text = L"this line has no url at all";
    auto m = scan_urls(text);
    CHECK(m.empty());
}

TEST_CASE("scan_urls - URL with anchor fragment") {
    std::wstring text = L"see https://docs.example.com/page#section here";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) ==
          L"https://docs.example.com/page#section");
}

TEST_CASE("scan_urls - multiple trailing punctuation chars are all trimmed") {
    // The trim loop must strip a run of trailing punct, not just one char.
    std::wstring text = L"https://x.com/path.,;";
    auto m = scan_urls(text);
    REQUIRE(m.size() == 1);
    CHECK(text.substr(m[0].start, m[0].end - m[0].start) == L"https://x.com/path");
}
