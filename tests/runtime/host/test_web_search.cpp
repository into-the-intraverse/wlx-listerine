#include <doctest/doctest.h>
#include "runtime/host/web_search.h"
#include "runtime/host/link_actions.h"

using namespace wlx::runtime::host;

TEST_CASE("build_google_search_url empty input returns empty") {
    CHECK(build_google_search_url(L"").empty());
}

TEST_CASE("build_google_search_url whitespace-only returns empty") {
    CHECK(build_google_search_url(L"   \t\r\n  ").empty());
}

TEST_CASE("build_google_search_url plain ASCII") {
    auto url = build_google_search_url(L"hello world");
    CHECK(url == L"https://www.google.com/search?q=hello%20world");
}

TEST_CASE("build_google_search_url collapses internal whitespace runs") {
    auto url = build_google_search_url(L"foo   bar\n\tbaz");
    CHECK(url == L"https://www.google.com/search?q=foo%20bar%20baz");
}

TEST_CASE("build_google_search_url trims leading/trailing whitespace") {
    auto url = build_google_search_url(L"  hello  ");
    CHECK(url == L"https://www.google.com/search?q=hello");
}

TEST_CASE("build_google_search_url percent-encodes reserved chars") {
    // ?, #, &, =, +, /, : must be encoded
    auto url = build_google_search_url(L"a?b#c&d=e+f/g:h");
    CHECK(url == L"https://www.google.com/search?q=a%3Fb%23c%26d%3De%2Bf%2Fg%3Ah");
}

TEST_CASE("build_google_search_url unreserved chars stay literal") {
    // RFC 3986 unreserved: A-Z a-z 0-9 - . _ ~
    auto url = build_google_search_url(L"abcXYZ-._~012");
    CHECK(url == L"https://www.google.com/search?q=abcXYZ-._~012");
}

TEST_CASE("build_google_search_url percent-encodes UTF-8 bytes") {
    // U+00E9 'é' = UTF-8 0xC3 0xA9
    auto url = build_google_search_url(L"café");
    CHECK(url == L"https://www.google.com/search?q=caf%C3%A9");
}

TEST_CASE("build_google_search_url percent-encodes CJK") {
    // U+4E2D '中' = UTF-8 0xE4 0xB8 0xAD
    auto url = build_google_search_url(L"中");
    CHECK(url == L"https://www.google.com/search?q=%E4%B8%AD");
}

TEST_CASE("build_google_search_url truncates over 1500 wchars before encoding") {
    std::wstring long_query(2000, L'a');
    auto url = build_google_search_url(long_query);
    // Prefix + exactly 1500 'a' characters
    std::wstring expected = L"https://www.google.com/search?q=" + std::wstring(1500, L'a');
    CHECK(url == expected);
}

// search_with_google delegates to open_external_url, which gates every url
// on this allowlist — tested here, next to its only in-tree delegator.

TEST_CASE("is_openable_url allows the four allowlisted schemes, case-insensitively") {
    CHECK(is_openable_url(L"https://example.com"));
    CHECK(is_openable_url(L"http://example.com"));
    CHECK(is_openable_url(L"ftp://files.example.com/a.zip"));
    CHECK(is_openable_url(L"mailto:user@example.com"));
    CHECK(is_openable_url(L"HTTPS://EXAMPLE.COM/PATH"));
    CHECK(is_openable_url(L"MailTo:user@example.com"));
}

TEST_CASE("is_openable_url rejects a scheme with no body") {
    CHECK_FALSE(is_openable_url(L"https://"));
    CHECK_FALSE(is_openable_url(L"mailto:"));
}

TEST_CASE("is_openable_url blocks launch-capable schemes and paths") {
    CHECK_FALSE(is_openable_url(L"file:///C:/Windows/System32/calc.exe"));
    CHECK_FALSE(is_openable_url(L"file://evil/share/x.exe"));
    CHECK_FALSE(is_openable_url(L"ms-msdt:id PCWDiagnostic"));
    CHECK_FALSE(is_openable_url(L"javascript:alert(1)"));
    CHECK_FALSE(is_openable_url(L"\\\\evil\\share\\x.exe"));
    CHECK_FALSE(is_openable_url(L"C:/evil.exe"));
    CHECK_FALSE(is_openable_url(L"C:\\evil.exe"));
    CHECK_FALSE(is_openable_url(L""));
}

TEST_CASE("is_openable_url blocks local paths of every shape (no .toml exemption)") {
    // EditConfig now routes through the trusted open_config_file helper, so
    // the allowlist carries no content-based exemption a crafted href could hit.
    CHECK_FALSE(is_openable_url(L"D:\\plugins\\wlx\\wlx-listerine-md.toml"));
    CHECK_FALSE(is_openable_url(L"c:/plugins/wlx-listerine-colorizer.TOML"));
    CHECK_FALSE(is_openable_url(L"\\\\server\\share\\wlx-listerine-md.toml"));
    CHECK_FALSE(is_openable_url(L"D:\\plugins\\wlx\\not-a-config.exe"));
}
