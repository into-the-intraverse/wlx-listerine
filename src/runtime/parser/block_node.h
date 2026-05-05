#pragma once

#include "runtime/parser/inline_node.h"
#include "runtime/parser/source_range.h"

#include <string>
#include <vector>

enum class BlockType {
    Document,
    Heading,
    Paragraph,
    List,
    ListItem,
    BlockQuote,
    HorizontalRule,
    Table,
    TableRow,
    TableCell,
    TaskList,
    CodeFence
};

enum class ListStyle {
    Unordered,
    Ordered
};

struct BlockNode {
    BlockType type = BlockType::Paragraph;
    std::vector<InlineNode> inlines;
    std::vector<BlockNode> children;

    // Heading
    int heading_level = 0;

    // List
    ListStyle list_style = ListStyle::Unordered;
    int list_start = 1;

    // TaskList item
    bool task_checked = false;

    // CodeFence
    std::wstring code_language;

    // Table
    int table_columns = 0;

    // TableCell
    bool is_header = false;
    enum class CellAlign { Default, Left, Center, Right };
    CellAlign cell_align = CellAlign::Default;

    SourceRange source;
};
