#include <doctest/doctest.h>
#include "runtime/layout/inline_layout.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <wrl/client.h>
#include <cstring>

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
