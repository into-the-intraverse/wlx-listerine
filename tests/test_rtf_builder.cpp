#include <doctest/doctest.h>
#include "rtf_builder.h"

static Config cfg() { return default_config(); }

TEST_CASE("RTF output starts with rtf1 header and ends with closing brace") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("Hello", 5);
    CHECK(rtf.substr(0, 10) == "{\\rtf1\\ans");
    CHECK(rtf.back() == '}');
}

TEST_CASE("RTF contains font table with body and code fonts") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("x", 1);
    CHECK(rtf.find("\\fonttbl") != std::string::npos);
    CHECK(rtf.find("Segoe UI") != std::string::npos);
    CHECK(rtf.find("Consolas") != std::string::npos);
}

TEST_CASE("RTF contains color table") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("x", 1);
    CHECK(rtf.find("\\colortbl;") != std::string::npos);
}

TEST_CASE("dark mode uses dark color scheme") {
    RtfBuilder b(cfg(), true);
    auto rtf = b.build("x", 1);
    // Dark text color is #D4D4D4 = rgb(212,212,212)
    CHECK(rtf.find("\\red212\\green212\\blue212") != std::string::npos);
}

TEST_CASE("plain text appears in output") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("Hello world", 11);
    CHECK(rtf.find("Hello world") != std::string::npos);
}

TEST_CASE("RTF special chars are escaped") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("a\\b{c}d", 7);
    CHECK(rtf.find("a\\\\b\\{c\\}d") != std::string::npos);
}

TEST_CASE("non-ASCII UTF-8 emits RTF unicode escapes") {
    // U+00E9 (e-acute) = UTF-8: 0xC3 0xA9 = RTF: \u233?
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("\xC3\xA9", 2);
    CHECK(rtf.find("\\u233?") != std::string::npos);
}

TEST_CASE("H1 renders with large bold font and bottom border") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("# Title", 7);
    CHECK(rtf.find("\\fs56") != std::string::npos);  // 28pt
    CHECK(rtf.find("\\b") != std::string::npos);
    CHECK(rtf.find("\\cf2") != std::string::npos);   // heading color
    CHECK(rtf.find("\\brdrb") != std::string::npos);  // bottom border
    CHECK(rtf.find("Title") != std::string::npos);
}

TEST_CASE("H3 renders with medium font") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("### Sub", 7);
    CHECK(rtf.find("\\fs40") != std::string::npos);  // 20pt
}

TEST_CASE("Bullet list renders with bullet char and indent") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("- item1\n- item2", 15);
    CHECK(rtf.find("\\li360") != std::string::npos);
    CHECK(rtf.find("item1") != std::string::npos);
    CHECK(rtf.find("item2") != std::string::npos);
}

TEST_CASE("Ordered list renders with number") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("1. first\n2. second", 18);
    CHECK(rtf.find("\\li360") != std::string::npos);
    CHECK(rtf.find("1.") != std::string::npos);
    CHECK(rtf.find("first") != std::string::npos);
}

TEST_CASE("Code block uses monospace font and highlight") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("```\ncode here\n```", 17);
    CHECK(rtf.find("\\f1") != std::string::npos);    // Consolas
    CHECK(rtf.find("\\highlight5") != std::string::npos);
    CHECK(rtf.find("code here") != std::string::npos);
}

TEST_CASE("Blockquote uses indent and blockquote color") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("> quoted", 8);
    CHECK(rtf.find("\\li720") != std::string::npos);
    CHECK(rtf.find("\\cf4") != std::string::npos);   // blockquote color
    CHECK(rtf.find("quoted") != std::string::npos);
}

TEST_CASE("Horizontal rule renders as bordered paragraph") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("---", 3);
    CHECK(rtf.find("\\brdrb\\brdrs") != std::string::npos);
}

TEST_CASE("Task list renders checkbox characters") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("- [ ] todo\n- [x] done", 21);
    CHECK(rtf.find("todo") != std::string::npos);
    CHECK(rtf.find("done") != std::string::npos);
}

TEST_CASE("Bold text uses \\b") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("**bold**", 8);
    CHECK(rtf.find("\\b ") != std::string::npos);
    CHECK(rtf.find("\\b0") != std::string::npos);
    CHECK(rtf.find("bold") != std::string::npos);
}

TEST_CASE("Italic text uses \\i") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("*italic*", 8);
    CHECK(rtf.find("\\i ") != std::string::npos);
    CHECK(rtf.find("\\i0") != std::string::npos);
}

TEST_CASE("Strikethrough uses \\strike") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("~~del~~", 7);
    CHECK(rtf.find("\\strike ") != std::string::npos);
    CHECK(rtf.find("\\strike0") != std::string::npos);
}

TEST_CASE("Inline code uses monospace font and highlight") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("`code`", 6);
    CHECK(rtf.find("\\f1") != std::string::npos);
    CHECK(rtf.find("\\highlight5") != std::string::npos);
}

TEST_CASE("Link stores URL and uses link color") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("[click](https://example.com)", 28);
    CHECK(rtf.find("\\cf3") != std::string::npos);   // link color
    CHECK(rtf.find("\\ul") != std::string::npos);     // underline
    CHECK(rtf.find("click") != std::string::npos);
    auto& links = b.links();
    REQUIRE(links.size() == 1);
    CHECK(links[0].url == "https://example.com");
    CHECK(links[0].char_start < links[0].char_end);
}

TEST_CASE("Image renders as alt text in brackets") {
    RtfBuilder b(cfg(), false);
    auto rtf = b.build("![photo](img.png)", 17);
    CHECK(rtf.find("[Image: photo]") != std::string::npos);
}
