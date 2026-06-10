#include <doctest/doctest.h>
#include "runtime/layout/inline_layout.h"
#include "runtime/layout/code_fence_layout.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <string>

using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;
using namespace wlx::runtime::theme;
using Microsoft::WRL::ComPtr;

static ComPtr<IDWriteFactory> dwf() {
    ComPtr<IDWriteFactory> f;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(f.GetAddressOf()));
    return f;
}

TEST_CASE("build_code_fence_layout measures a fenced block") {
    auto factory = dwf();
    REQUIRE(factory);
    ThemeService theme;
    ComPtr<IDWriteTextFormat> code;
    factory->CreateTextFormat(theme.fonts().code_family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        theme.fonts().code_size, L"", code.GetAddressOf());
    code->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    CodeFenceInput in;
    in.code_text = L"int x = 1;\nint y = 2;";
    in.code_language = "";
    in.max_width = 760.0f;
    in.wrap_code = false;
    in.dark_mode = false;
    in.core = nullptr;        // no colorizer -> plain layout, still measures
    in.default_language = "";
    auto r = build_code_fence_layout(factory.Get(), code.Get(), in);
    CHECK(r.layout != nullptr);
    CHECK(r.height > 0.0f);
}

TEST_CASE("utf8_with_offsets: surrogate pair encodes as 4-byte UTF-8, offsets aligned") {
    // U+1F600 = UTF-16 pair D83D DE00 = UTF-8 F0 9F 98 80. The old per-wchar
    // encoder emitted two 3-byte CESU-8 units (invalid UTF-8 for tree-sitter)
    // and the offset table drifted by 2 bytes per emoji.
    std::wstring text = L"a\U0001F600b";
    REQUIRE(text.size() == 4);  // 'a' + high + low surrogate + 'b'

    auto m = utf8_with_offsets(text);

    REQUIRE(m.utf8.size() == 6);  // 1 + 4 + 1 bytes
    CHECK(static_cast<unsigned char>(m.utf8[1]) == 0xF0);
    CHECK(static_cast<unsigned char>(m.utf8[2]) == 0x9F);
    CHECK(static_cast<unsigned char>(m.utf8[3]) == 0x98);
    CHECK(static_cast<unsigned char>(m.utf8[4]) == 0x80);

    REQUIRE(m.wchar_to_byte.size() == text.size() + 1);  // + end sentinel
    CHECK(m.wchar_to_byte[0] == 0);  // 'a'
    CHECK(m.wchar_to_byte[1] == 1);  // high surrogate: pair start
    CHECK(m.wchar_to_byte[2] == 1);  // low surrogate shares the pair start
    CHECK(m.wchar_to_byte[3] == 5);  // 'b' — stays aligned after the emoji
    CHECK(m.wchar_to_byte[4] == 6);  // sentinel == total bytes
}

TEST_CASE("build_inline_layout produces a measured layout for a paragraph") {
    auto factory = dwf();
    REQUIRE(factory);
    MarkdownParser p;
    Document doc = p.parse("Hello world", 11);
    REQUIRE(!doc.blocks.empty());

    ThemeService theme;
    ComPtr<IDWriteTextFormat> body;
    factory->CreateTextFormat(theme.fonts().body_family.c_str(), nullptr,
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        theme.fonts().body_size, L"", body.GetAddressOf());
    body->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    auto r = build_inline_layout(factory.Get(), doc.blocks[0].inlines, 800.0f,
                                 theme.palette(false).text, body.Get(),
                                 /*force_bold=*/false, theme.fonts(), theme.palette(false));
    CHECK(r.layout != nullptr);
    CHECK(r.full_text == L"Hello world");
    CHECK(r.height > 0.0f);
}
