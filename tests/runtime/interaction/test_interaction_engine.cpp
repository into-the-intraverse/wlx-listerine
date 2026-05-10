// tests/runtime/interaction/test_interaction_engine.cpp
#include <doctest/doctest.h>
#include "runtime/interaction/interaction_engine.h"
#include "runtime/layout/layout_engine.h"
#include "runtime/parser/markdown_parser.h"
#include "runtime/theme/theme_service.h"

#include <dwrite.h>
#include <wrl/client.h>
#include <cstring>

using namespace wlx::runtime::interaction;
using namespace wlx::runtime::layout;
using namespace wlx::runtime::parser;
using namespace wlx::runtime::theme;

using Microsoft::WRL::ComPtr;

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

TEST_CASE("InteractionEngine::anchor_y - exact match") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# General Code Formatting\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    auto y = eng.anchor_y(L"general-code-formatting");
    CHECK(y.has_value());
}

TEST_CASE("InteractionEngine::anchor_y - trailing dash on fragment is normalized") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# General Code Formatting\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    // GitHub-style TOC links carry the trailing dash for headings ending
    // in whitespace + non-alnum; ours strip it from stored slugs but
    // anchor_y must still resolve it.
    auto y = eng.anchor_y(L"general-code-formatting-");
    CHECK(y.has_value());
}

TEST_CASE("InteractionEngine::anchor_y - mixed-case fragment is normalized") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# Style Guide\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    auto y1 = eng.anchor_y(L"Style-Guide");
    auto y2 = eng.anchor_y(L"STYLE-GUIDE");
    auto y3 = eng.anchor_y(L"style-guide");
    CHECK(y1.has_value());
    CHECK(y2.has_value());
    CHECK(y3.has_value());
}

TEST_CASE("InteractionEngine::anchor_y - leading dash on fragment is normalized") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# Intro\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    // Defensive: a malformed fragment with a stray leading dash should
    // still resolve, even though slugify ensures stored slugs never have one.
    auto y = eng.anchor_y(L"-intro");
    CHECK(y.has_value());
}

TEST_CASE("InteractionEngine::anchor_y - unknown fragment returns nullopt") {
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# Intro\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    auto y = eng.anchor_y(L"does-not-exist");
    CHECK_FALSE(y.has_value());
}

TEST_CASE("InteractionEngine::anchor_y - all-dashes fragment returns nullopt") {
    // Stress the !s.empty() guards in normalize_fragment: '---' becomes
    // empty after the leading-dash strip, which must not UB on subsequent
    // operations.
    auto factory = create_dwrite_factory();
    REQUIRE(factory);
    auto doc = parse("# Intro\n\nbody");
    auto layout = do_layout(factory.Get(), doc);
    InteractionEngine eng(layout);
    auto y = eng.anchor_y(L"---");
    CHECK_FALSE(y.has_value());
}
