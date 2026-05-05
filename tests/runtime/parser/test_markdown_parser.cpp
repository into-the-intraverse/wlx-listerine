#include <doctest/doctest.h>
#include "runtime/parser/markdown_parser.h"

using namespace wlx::runtime::parser;

// Helper: parse a string literal (auto-computes length)
static Document parse(const char* md) {
    MarkdownParser p;
    return p.parse(md, std::strlen(md));
}

TEST_CASE("Empty input") {
    auto doc = parse("");
    CHECK(doc.blocks.empty());
}

TEST_CASE("Plain text") {
    auto doc = parse("Hello world");
    REQUIRE(doc.blocks.size() == 1);
    CHECK(doc.blocks[0].type == BlockType::Paragraph);
    REQUIRE(doc.blocks[0].inlines.size() == 1);
    CHECK(doc.blocks[0].inlines[0].type == InlineType::Text);
    CHECK(doc.blocks[0].inlines[0].text == L"Hello world");
}

TEST_CASE("Heading levels") {
    const char* headings[] = {
        "# H1", "## H2", "### H3", "#### H4", "##### H5", "###### H6"
    };
    for (int i = 0; i < 6; i++) {
        auto doc = parse(headings[i]);
        REQUIRE(doc.blocks.size() == 1);
        CHECK(doc.blocks[0].type == BlockType::Heading);
        CHECK(doc.blocks[0].heading_level == i + 1);
        REQUIRE(!doc.blocks[0].inlines.empty());
        // The text should be "H1", "H2", etc.
        std::wstring expected = L"H" + std::to_wstring(i + 1);
        CHECK(doc.blocks[0].inlines[0].text == expected);
    }
}

TEST_CASE("Bold text") {
    auto doc = parse("**bold**");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    REQUIRE(!inlines.empty());
    // Find the inline containing "bold"
    bool found_bold = false;
    for (auto& n : inlines) {
        if (n.text == L"bold") {
            CHECK(n.bold == true);
            found_bold = true;
        }
    }
    CHECK(found_bold);
}

TEST_CASE("Italic text") {
    auto doc = parse("*italic*");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    bool found = false;
    for (auto& n : inlines) {
        if (n.text == L"italic") {
            CHECK(n.italic == true);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("Bold italic") {
    auto doc = parse("***both***");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    bool found = false;
    for (auto& n : inlines) {
        if (n.text == L"both") {
            CHECK(n.bold == true);
            CHECK(n.italic == true);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("Strikethrough") {
    auto doc = parse("~~struck~~");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    bool found = false;
    for (auto& n : inlines) {
        if (n.text == L"struck") {
            CHECK(n.strikethrough == true);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("Inline code") {
    auto doc = parse("`code`");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    bool found = false;
    for (auto& n : inlines) {
        if (n.text == L"code") {
            CHECK(n.code == true);
            CHECK(n.type == InlineType::InlineCode);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("Link - external URL") {
    auto doc = parse("[text](https://example.com)");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    bool found = false;
    for (auto& n : inlines) {
        if (n.text == L"text" && n.link.has_value()) {
            CHECK(n.link->kind == LinkKind::ExternalUrl);
            CHECK(n.link->url == L"https://example.com");
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("Internal link") {
    auto doc = parse("[text](#section)");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    bool found = false;
    for (auto& n : inlines) {
        if (n.text == L"text" && n.link.has_value()) {
            CHECK(n.link->kind == LinkKind::InternalAnchor);
            CHECK(n.link->url == L"");
            CHECK(n.link->anchor_fragment == L"section");
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("Relative link") {
    auto doc = parse("[text](other.md)");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    bool found = false;
    for (auto& n : inlines) {
        if (n.text == L"text" && n.link.has_value()) {
            CHECK(n.link->kind == LinkKind::RelativeDoc);
            CHECK(n.link->url == L"other.md");
            CHECK(n.link->anchor_fragment == L"");
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("Relative link with anchor") {
    auto doc = parse("[text](other.md#heading)");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    bool found = false;
    for (auto& n : inlines) {
        if (n.text == L"text" && n.link.has_value()) {
            CHECK(n.link->kind == LinkKind::RelativeDoc);
            CHECK(n.link->url == L"other.md");
            CHECK(n.link->anchor_fragment == L"heading");
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("Unordered list") {
    auto doc = parse("- item1\n- item2");
    REQUIRE(doc.blocks.size() == 1);
    auto& list = doc.blocks[0];
    CHECK(list.type == BlockType::List);
    CHECK(list.list_style == ListStyle::Unordered);
    REQUIRE(list.children.size() == 2);
    CHECK(list.children[0].type == BlockType::ListItem);
    CHECK(list.children[1].type == BlockType::ListItem);

    // In a tight list, md4c omits the Paragraph wrapper.
    // Text appears directly in the ListItem's inlines.
    REQUIRE(!list.children[0].inlines.empty());
    CHECK(list.children[0].inlines[0].text == L"item1");

    REQUIRE(!list.children[1].inlines.empty());
    CHECK(list.children[1].inlines[0].text == L"item2");
}

TEST_CASE("Ordered list") {
    auto doc = parse("1. first\n2. second");
    REQUIRE(doc.blocks.size() == 1);
    auto& list = doc.blocks[0];
    CHECK(list.type == BlockType::List);
    CHECK(list.list_style == ListStyle::Ordered);
    CHECK(list.list_start == 1);
    REQUIRE(list.children.size() == 2);
    CHECK(list.children[0].type == BlockType::ListItem);
    CHECK(list.children[1].type == BlockType::ListItem);
}

TEST_CASE("Nested list") {
    auto doc = parse("- outer\n  - inner");
    REQUIRE(doc.blocks.size() == 1);
    auto& outer_list = doc.blocks[0];
    CHECK(outer_list.type == BlockType::List);
    // The outer list has at least one ListItem
    REQUIRE(!outer_list.children.empty());
    auto& outer_item = outer_list.children[0];
    CHECK(outer_item.type == BlockType::ListItem);

    // The outer item should contain a nested list as a child
    // md4c structures: ListItem -> [Paragraph, List]
    bool found_nested = false;
    for (auto& child : outer_item.children) {
        if (child.type == BlockType::List) {
            found_nested = true;
            REQUIRE(!child.children.empty());
            CHECK(child.children[0].type == BlockType::ListItem);
        }
    }
    CHECK(found_nested);
}

TEST_CASE("Blockquote") {
    auto doc = parse("> quoted");
    REQUIRE(doc.blocks.size() == 1);
    CHECK(doc.blocks[0].type == BlockType::BlockQuote);
    // Blockquote contains a Paragraph child
    REQUIRE(!doc.blocks[0].children.empty());
    auto& para = doc.blocks[0].children[0];
    CHECK(para.type == BlockType::Paragraph);
    REQUIRE(!para.inlines.empty());
    CHECK(para.inlines[0].text == L"quoted");
}

TEST_CASE("Horizontal rule") {
    auto doc = parse("---");
    REQUIRE(doc.blocks.size() == 1);
    CHECK(doc.blocks[0].type == BlockType::HorizontalRule);
}

TEST_CASE("Code fence") {
    auto doc = parse("```cpp\ncode\n```");
    REQUIRE(doc.blocks.size() == 1);
    auto& block = doc.blocks[0];
    CHECK(block.type == BlockType::CodeFence);
    CHECK(block.code_language == L"cpp");
    REQUIRE(!block.inlines.empty());
    // Code block text includes trailing newline from md4c
    bool found_code = false;
    for (auto& n : block.inlines) {
        if (n.text.find(L"code") != std::wstring::npos) {
            found_code = true;
        }
    }
    CHECK(found_code);
}

TEST_CASE("Task list") {
    auto doc = parse("- [x] done\n- [ ] todo");
    REQUIRE(doc.blocks.size() == 1);
    auto& list = doc.blocks[0];
    CHECK(list.type == BlockType::List);
    REQUIRE(list.children.size() == 2);

    auto& done = list.children[0];
    CHECK(done.type == BlockType::TaskList);
    CHECK(done.task_checked == true);

    auto& todo = list.children[1];
    CHECK(todo.type == BlockType::TaskList);
    CHECK(todo.task_checked == false);
}

TEST_CASE("Table") {
    auto doc = parse("| a | b |\n|---|---|\n| 1 | 2 |");
    REQUIRE(doc.blocks.size() == 1);
    auto& table = doc.blocks[0];
    CHECK(table.type == BlockType::Table);
    CHECK(table.table_columns == 2);

    // Should have rows (header + body)
    REQUIRE(table.children.size() >= 2);

    // First row should have header cells
    auto& header_row = table.children[0];
    CHECK(header_row.type == BlockType::TableRow);
    REQUIRE(header_row.children.size() == 2);
    CHECK(header_row.children[0].type == BlockType::TableCell);
    CHECK(header_row.children[0].is_header == true);
    REQUIRE(!header_row.children[0].inlines.empty());
    CHECK(header_row.children[0].inlines[0].text == L"a");
    CHECK(header_row.children[1].inlines[0].text == L"b");

    // Second row should have body cells
    auto& body_row = table.children[1];
    CHECK(body_row.type == BlockType::TableRow);
    REQUIRE(body_row.children.size() == 2);
    CHECK(body_row.children[0].is_header == false);
    REQUIRE(!body_row.children[0].inlines.empty());
    CHECK(body_row.children[0].inlines[0].text == L"1");
    CHECK(body_row.children[1].inlines[0].text == L"2");
}

TEST_CASE("Multiple paragraphs") {
    auto doc = parse("para1\n\npara2");
    REQUIRE(doc.blocks.size() == 2);
    CHECK(doc.blocks[0].type == BlockType::Paragraph);
    CHECK(doc.blocks[1].type == BlockType::Paragraph);
    REQUIRE(!doc.blocks[0].inlines.empty());
    CHECK(doc.blocks[0].inlines[0].text == L"para1");
    REQUIRE(!doc.blocks[1].inlines.empty());
    CHECK(doc.blocks[1].inlines[0].text == L"para2");
}

TEST_CASE("Mixed inline") {
    auto doc = parse("normal **bold** normal");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    // Should have at least 3 inline nodes: text, bold text, text
    REQUIRE(inlines.size() >= 3);

    // First should be non-bold
    CHECK(inlines[0].text == L"normal ");
    CHECK(inlines[0].bold == false);

    // Middle should be bold
    CHECK(inlines[1].text == L"bold");
    CHECK(inlines[1].bold == true);

    // Last should be non-bold
    CHECK(inlines[2].text == L" normal");
    CHECK(inlines[2].bold == false);
}

TEST_CASE("Soft break") {
    auto doc = parse("line1\nline2");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    // Expect: Text("line1"), SoftBreak, Text("line2")
    REQUIRE(inlines.size() == 3);
    CHECK(inlines[0].type == InlineType::Text);
    CHECK(inlines[0].text == L"line1");
    CHECK(inlines[1].type == InlineType::SoftBreak);
    CHECK(inlines[2].type == InlineType::Text);
    CHECK(inlines[2].text == L"line2");
}

TEST_CASE("Hard break") {
    // Two trailing spaces before newline = hard break
    auto doc = parse("line1  \nline2");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    // Expect: Text("line1"), HardBreak, Text("line2")
    REQUIRE(inlines.size() == 3);
    CHECK(inlines[0].type == InlineType::Text);
    CHECK(inlines[0].text == L"line1");
    CHECK(inlines[1].type == InlineType::HardBreak);
    CHECK(inlines[2].type == InlineType::Text);
    CHECK(inlines[2].text == L"line2");
}

TEST_CASE("HTML entity") {
    auto doc = parse("&amp;");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    REQUIRE(!inlines.empty());
    CHECK(inlines[0].type == InlineType::Text);
    CHECK(inlines[0].text == L"&");
}

TEST_CASE("Unicode text") {
    // Russian: "Привет мир" in UTF-8
    const char* md = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"
                     " "
                     "\xD0\xBC\xD0\xB8\xD1\x80";
    auto doc = parse(md);
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    REQUIRE(!inlines.empty());
    CHECK(inlines[0].text == L"\u041F\u0440\u0438\u0432\u0435\u0442 \u043C\u0438\u0440");
}

TEST_CASE("parser_version returns 1") {
    CHECK(MarkdownParser::parser_version() == 1);
}

TEST_CASE("Code fence without language") {
    auto doc = parse("```\nsome code\n```");
    REQUIRE(doc.blocks.size() == 1);
    CHECK(doc.blocks[0].type == BlockType::CodeFence);
    CHECK(doc.blocks[0].code_language.empty());
}

TEST_CASE("Multiple HTML entities") {
    auto doc = parse("&lt;div&gt;");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    // Should have 3 inline nodes: "<", "div", ">"
    REQUIRE(inlines.size() == 3);
    CHECK(inlines[0].text == L"<");
    CHECK(inlines[1].text == L"div");
    CHECK(inlines[2].text == L">");
}

TEST_CASE("Numeric HTML entity") {
    auto doc = parse("&#169;"); // copyright symbol
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    REQUIRE(!inlines.empty());
    CHECK(inlines[0].text == L"\u00A9");
}

TEST_CASE("Hex HTML entity") {
    auto doc = parse("&#x00A9;"); // copyright symbol via hex
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    REQUIRE(!inlines.empty());
    CHECK(inlines[0].text == L"\u00A9");
}

TEST_CASE("Bold inside link") {
    auto doc = parse("[**bold link**](https://example.com)");
    REQUIRE(doc.blocks.size() == 1);
    auto& inlines = doc.blocks[0].inlines;
    bool found = false;
    for (auto& n : inlines) {
        if (n.text == L"bold link") {
            CHECK(n.bold == true);
            CHECK(n.link.has_value());
            CHECK(n.link->kind == LinkKind::ExternalUrl);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("Ordered list with custom start") {
    auto doc = parse("5. fifth\n6. sixth");
    REQUIRE(doc.blocks.size() == 1);
    auto& list = doc.blocks[0];
    CHECK(list.type == BlockType::List);
    CHECK(list.list_style == ListStyle::Ordered);
    CHECK(list.list_start == 5);
}

TEST_CASE("Deeply nested blockquote") {
    auto doc = parse("> > nested");
    REQUIRE(doc.blocks.size() == 1);
    CHECK(doc.blocks[0].type == BlockType::BlockQuote);
    // Inner blockquote
    REQUIRE(!doc.blocks[0].children.empty());
    auto& inner = doc.blocks[0].children[0];
    CHECK(inner.type == BlockType::BlockQuote);
    REQUIRE(!inner.children.empty());
    auto& para = inner.children[0];
    CHECK(para.type == BlockType::Paragraph);
    REQUIRE(!para.inlines.empty());
    CHECK(para.inlines[0].text == L"nested");
}

TEST_CASE("Reuse parser instance") {
    MarkdownParser p;
    auto doc1 = p.parse("# First", 7);
    auto doc2 = p.parse("# Second", 8);

    REQUIRE(doc1.blocks.size() == 1);
    CHECK(doc1.blocks[0].inlines[0].text == L"First");

    REQUIRE(doc2.blocks.size() == 1);
    CHECK(doc2.blocks[0].inlines[0].text == L"Second");
}
