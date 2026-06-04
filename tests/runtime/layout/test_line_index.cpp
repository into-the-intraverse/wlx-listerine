#include "runtime/layout/line_index.h"
#include "runtime/layout/layout_document.h"

#include <doctest/doctest.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>

using namespace wlx::runtime::layout;
using wlx::runtime::parser::BlockType;
using Microsoft::WRL::ComPtr;

static LayoutBlock make_block(BlockType type, float top, const std::wstring& text,
                              IDWriteTextLayout* layout) {
    LayoutBlock b;
    b.type = type;
    b.rect = D2D1::RectF(0.0f, top, 100.0f, top + 10.0f);
    TextRun r;
    r.text = text;
    r.rect = b.rect;
    r.layout = layout;  // ComPtr operator= AddRefs; nullptr is fine
    b.text_runs.push_back(std::move(r));
    return b;
}

TEST_CASE("build_line_index: one logical line per simple block") {
    LayoutDocument doc;
    doc.blocks.push_back(make_block(BlockType::Heading, 0.0f, L"Title", nullptr));
    doc.blocks.push_back(make_block(BlockType::Paragraph, 20.0f, L"para", nullptr));
    doc.blocks.push_back(make_block(BlockType::ListItem, 40.0f, L"item", nullptr));

    build_line_index(doc);

    REQUIRE(doc.line_tops.size() == 3);
    CHECK(doc.line_tops[0] == doctest::Approx(0.0f));
    CHECK(doc.line_tops[1] == doctest::Approx(20.0f));
    CHECK(doc.line_tops[2] == doctest::Approx(40.0f));
}

TEST_CASE("build_line_index: table-row cells sharing a top collapse to one line") {
    LayoutDocument doc;
    doc.blocks.push_back(make_block(BlockType::TableCell, 0.0f, L"a", nullptr));
    doc.blocks.push_back(make_block(BlockType::TableCell, 0.0f, L"b", nullptr));   // same row
    doc.blocks.push_back(make_block(BlockType::TableCell, 18.0f, L"c", nullptr));  // next row

    build_line_index(doc);

    CHECK(doc.line_tops.size() == 2);
}

TEST_CASE("build_line_index: hard breaks in a paragraph add lines; soft wrap does not") {
    ComPtr<IDWriteFactory> factory;
    REQUIRE(SUCCEEDED(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(factory.GetAddressOf()))));

    ComPtr<IDWriteTextFormat> fmt;
    REQUIRE(SUCCEEDED(factory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"", fmt.GetAddressOf())));

    std::wstring text = L"line one\nline two";  // '\n' == hard break
    ComPtr<IDWriteTextLayout> tl;
    REQUIRE(SUCCEEDED(factory->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.size()), fmt.Get(),
        1000.0f, 1000.0f, tl.GetAddressOf())));

    LayoutDocument doc;
    doc.blocks.push_back(make_block(BlockType::Paragraph, 0.0f, text, tl.Get()));

    build_line_index(doc);

    REQUIRE(doc.line_tops.size() == 2);
    CHECK(doc.line_tops[0] == doctest::Approx(0.0f));
    CHECK(doc.line_tops[1] > 0.0f);  // second segment sits below the first
}
