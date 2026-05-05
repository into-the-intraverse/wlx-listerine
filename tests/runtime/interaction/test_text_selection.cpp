#include <doctest/doctest.h>
#include "runtime/interaction/text_selection.h"
#include "runtime/layout/layout_engine.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/theme/theme_service.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cstring>
#include <algorithm>

using Microsoft::WRL::ComPtr;

TEST_CASE("TextPosition default is invalid") {
    TextPosition pos;
    CHECK(pos.block_index == -1);
    CHECK(pos.char_offset == 0);
    CHECK_FALSE(pos.valid());
}

TEST_CASE("TextPosition with non-negative block_index is valid") {
    TextPosition pos{0, 0};
    CHECK(pos.valid());

    TextPosition pos2{5, 10};
    CHECK(pos2.valid());
}

TEST_CASE("TextPosition with negative block_index is invalid") {
    TextPosition pos{-1, 0};
    CHECK_FALSE(pos.valid());

    TextPosition pos2{-3, 5};
    CHECK_FALSE(pos2.valid());
}

TEST_CASE("TextPosition equality") {
    TextPosition a{1, 5};
    TextPosition b{1, 5};
    TextPosition c{1, 6};
    TextPosition d{2, 5};

    CHECK(a == b);
    CHECK_FALSE(a == c);
    CHECK_FALSE(a == d);

    CHECK_FALSE(a != b);
    CHECK(a != c);
    CHECK(a != d);
}

TEST_CASE("TextPosition less-than compares block_index first") {
    TextPosition a{0, 10};
    TextPosition b{1, 0};

    CHECK(a < b);
    CHECK_FALSE(b < a);
    CHECK_FALSE(a < a);
}

TEST_CASE("TextPosition less-than compares char_offset within same block") {
    TextPosition a{2, 3};
    TextPosition b{2, 7};

    CHECK(a < b);
    CHECK_FALSE(b < a);
}

TEST_CASE("TextPosition greater-than") {
    TextPosition a{1, 5};
    TextPosition b{0, 99};

    CHECK(a > b);
    CHECK_FALSE(b > a);
    CHECK_FALSE(a > a);
}

TEST_CASE("TextPosition less-or-equal") {
    TextPosition a{1, 5};
    TextPosition b{1, 5};
    TextPosition c{1, 6};

    CHECK(a <= b);
    CHECK(a <= c);
    CHECK_FALSE(c <= a);
}

TEST_CASE("TextPosition greater-or-equal") {
    TextPosition a{1, 5};
    TextPosition b{1, 5};
    TextPosition c{1, 4};

    CHECK(a >= b);
    CHECK(a >= c);
    CHECK_FALSE(c >= a);
}

TEST_CASE("TextPosition works with std::min and std::max") {
    TextPosition a{0, 5};
    TextPosition b{2, 3};

    CHECK(std::min(a, b) == a);
    CHECK(std::max(a, b) == b);

    TextPosition c{1, 0};
    TextPosition d{1, 10};

    CHECK(std::min(c, d) == c);
    CHECK(std::max(c, d) == d);
}

// --- Helpers for extract_selected_text tests ---

static ComPtr<IDWriteFactory> create_dwrite_factory() {
    ComPtr<IDWriteFactory> factory;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    return factory;
}

static Document parse(const char* md) {
    MarkdownParser p;
    return p.parse(md, std::strlen(md));
}

static LayoutDocument do_layout(IDWriteFactory* factory, const Document& doc,
                                float width = 800.0f) {
    ThemeService theme;
    LayoutEngine engine(factory, theme, false);
    return engine.layout(doc, width);
}

// --- extract_selected_text tests ---

TEST_CASE("extract_selected_text - single block partial") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("Hello World");
    auto layout = do_layout(factory.Get(), doc);
    REQUIRE(!layout.blocks.empty());

    TextPosition start{0, 2};
    TextPosition end{0, 7};
    auto text = extract_selected_text(layout, start, end);
    CHECK(text == L"llo W");
}

TEST_CASE("extract_selected_text - full block") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("Hello World");
    auto layout = do_layout(factory.Get(), doc);

    int para_idx = -1;
    for (int i = 0; i < static_cast<int>(layout.blocks.size()); i++) {
        if (layout.blocks[i].type == BlockType::Paragraph) { para_idx = i; break; }
    }
    REQUIRE(para_idx >= 0);

    auto& blk = layout.blocks[para_idx];
    int last_char = 0;
    for (auto& run : blk.text_runs) last_char += static_cast<int>(run.text.size());

    TextPosition start{para_idx, 0};
    TextPosition end{para_idx, last_char};
    auto text = extract_selected_text(layout, start, end);
    CHECK(text == L"Hello World");
}

TEST_CASE("extract_selected_text - cross block") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("First paragraph\n\nSecond paragraph");
    auto layout = do_layout(factory.Get(), doc);

    int first_para = -1, last_para = -1;
    for (int i = 0; i < static_cast<int>(layout.blocks.size()); i++) {
        if (layout.blocks[i].type == BlockType::Paragraph) {
            if (first_para < 0) first_para = i;
            last_para = i;
        }
    }
    REQUIRE(first_para >= 0);
    REQUIRE(last_para > first_para);

    TextPosition start{first_para, 0};
    int last_len = 0;
    for (auto& run : layout.blocks[last_para].text_runs)
        last_len += static_cast<int>(run.text.size());
    TextPosition end{last_para, last_len};

    auto text = extract_selected_text(layout, start, end);
    CHECK(text.find(L"First paragraph") != std::wstring::npos);
    CHECK(text.find(L"Second paragraph") != std::wstring::npos);
    CHECK(text.find(L"\r\n") != std::wstring::npos);
}

TEST_CASE("extract_selected_text - invalid positions") {
    LayoutDocument empty_layout;
    TextPosition invalid{};
    TextPosition valid{0, 0};
    CHECK(extract_selected_text(empty_layout, invalid, valid).empty());
    CHECK(extract_selected_text(empty_layout, valid, invalid).empty());
}

TEST_CASE("extract_selected_text - reversed range") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("Hello World");
    auto layout = do_layout(factory.Get(), doc);

    // Pass end before start — function should swap them
    TextPosition start{0, 7};
    TextPosition end{0, 2};
    auto text = extract_selected_text(layout, start, end);
    CHECK(text == L"llo W");
}

TEST_CASE("find_word_boundaries - middle of word") {
    std::wstring text = L"Hello beautiful world";
    auto [start, end] = find_word_boundaries(text, 8);
    CHECK(start == 6);
    CHECK(end == 15);
}

TEST_CASE("find_word_boundaries - start of text") {
    std::wstring text = L"Hello world";
    auto [start, end] = find_word_boundaries(text, 0);
    CHECK(start == 0);
    CHECK(end == 5);
}

TEST_CASE("find_word_boundaries - on whitespace") {
    std::wstring text = L"Hello world";
    auto [start, end] = find_word_boundaries(text, 5);
    CHECK(start == 5);
    CHECK(end == 6);
}

TEST_CASE("find_word_boundaries - end of text") {
    std::wstring text = L"Hello";
    auto [start, end] = find_word_boundaries(text, 4);
    CHECK(start == 0);
    CHECK(end == 5);
}
