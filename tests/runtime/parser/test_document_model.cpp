#include <doctest/doctest.h>
#include "runtime/parser/block_node.h"
#include "runtime/parser/document.h"
#include "runtime/parser/inline_node.h"
#include "runtime/parser/link_target.h"
#include "runtime/parser/source_range.h"

TEST_CASE("BlockNode default values") {
    BlockNode node;
    CHECK(node.type == BlockType::Paragraph);
    CHECK(node.heading_level == 0);
    CHECK(node.list_style == ListStyle::Unordered);
    CHECK(node.list_start == 1);
    CHECK(node.task_checked == false);
    CHECK(node.code_language.empty());
    CHECK(node.table_columns == 0);
    CHECK(node.is_header == false);
    CHECK(node.inlines.empty());
    CHECK(node.children.empty());
}

TEST_CASE("InlineNode default values") {
    InlineNode node;
    CHECK(node.type == InlineType::Text);
    CHECK(node.text.empty());
    CHECK(node.bold == false);
    CHECK(node.italic == false);
    CHECK(node.strikethrough == false);
    CHECK(node.code == false);
    CHECK_FALSE(node.link.has_value());
}

TEST_CASE("LinkTarget construction") {
    LinkTarget target{LinkKind::InternalAnchor, L"", L"section-1"};
    CHECK(target.kind == LinkKind::InternalAnchor);
    CHECK(target.anchor_fragment == L"section-1");

    LinkTarget ext{LinkKind::ExternalUrl, L"https://example.com", L""};
    CHECK(ext.kind == LinkKind::ExternalUrl);
    CHECK(ext.url == L"https://example.com");

    LinkTarget rel{LinkKind::RelativeDoc, L"other.md", L"heading"};
    CHECK(rel.kind == LinkKind::RelativeDoc);
    CHECK(rel.url == L"other.md");
    CHECK(rel.anchor_fragment == L"heading");
}

TEST_CASE("Document with blocks") {
    Document doc;

    BlockNode heading;
    heading.type = BlockType::Heading;
    heading.heading_level = 1;
    heading.inlines.push_back({InlineType::Text, L"Title"});

    BlockNode para;
    para.type = BlockType::Paragraph;
    para.inlines.push_back({InlineType::Text, L"Hello "});
    InlineNode bold_node;
    bold_node.type = InlineType::Text;
    bold_node.text = L"world";
    bold_node.bold = true;
    para.inlines.push_back(bold_node);

    doc.blocks.push_back(std::move(heading));
    doc.blocks.push_back(std::move(para));

    CHECK(doc.blocks.size() == 2);
    CHECK(doc.blocks[0].type == BlockType::Heading);
    CHECK(doc.blocks[0].heading_level == 1);
    CHECK(doc.blocks[0].inlines[0].text == L"Title");
    CHECK(doc.blocks[1].inlines[1].bold == true);
}

TEST_CASE("Nested list structure") {
    BlockNode list;
    list.type = BlockType::List;
    list.list_style = ListStyle::Ordered;
    list.list_start = 1;

    BlockNode item1;
    item1.type = BlockType::ListItem;
    item1.inlines.push_back({InlineType::Text, L"First"});

    BlockNode item2;
    item2.type = BlockType::ListItem;
    item2.inlines.push_back({InlineType::Text, L"Second"});

    // Nested sublist inside item2
    BlockNode sublist;
    sublist.type = BlockType::List;
    sublist.list_style = ListStyle::Unordered;

    BlockNode sub_item;
    sub_item.type = BlockType::ListItem;
    sub_item.inlines.push_back({InlineType::Text, L"Sub-item"});
    sublist.children.push_back(std::move(sub_item));

    item2.children.push_back(std::move(sublist));

    list.children.push_back(std::move(item1));
    list.children.push_back(std::move(item2));

    CHECK(list.children.size() == 2);
    CHECK(list.children[1].children.size() == 1);
    CHECK(list.children[1].children[0].type == BlockType::List);
    CHECK(list.children[1].children[0].list_style == ListStyle::Unordered);
}

TEST_CASE("InlineNode with link") {
    InlineNode link_node;
    link_node.type = InlineType::Link;
    link_node.text = L"click here";
    link_node.link = LinkTarget{LinkKind::ExternalUrl, L"https://example.com", L""};

    CHECK(link_node.link.has_value());
    CHECK(link_node.link->kind == LinkKind::ExternalUrl);
    CHECK(link_node.link->url == L"https://example.com");
}

TEST_CASE("CodeFence block") {
    BlockNode fence;
    fence.type = BlockType::CodeFence;
    fence.code_language = L"cpp";
    fence.inlines.push_back({InlineType::Text, L"int main() { return 0; }"});

    CHECK(fence.type == BlockType::CodeFence);
    CHECK(fence.code_language == L"cpp");
}

TEST_CASE("Table structure") {
    BlockNode table;
    table.type = BlockType::Table;
    table.table_columns = 3;

    BlockNode header_row;
    header_row.type = BlockType::TableRow;
    for (int i = 0; i < 3; ++i) {
        BlockNode cell;
        cell.type = BlockType::TableCell;
        cell.is_header = true;
        cell.inlines.push_back({InlineType::Text, L"H" + std::to_wstring(i)});
        header_row.children.push_back(std::move(cell));
    }
    table.children.push_back(std::move(header_row));

    CHECK(table.children.size() == 1);
    CHECK(table.children[0].children.size() == 3);
    CHECK(table.children[0].children[0].is_header == true);
}
