#pragma once

#include <cstdint>
#include <optional>
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

enum class InlineType {
    Text,
    Emphasis,
    Strong,
    InlineCode,
    Link,
    SoftBreak,
    HardBreak,
    EmojiText
};

enum class LinkKind {
    InternalAnchor,
    RelativeDoc,
    ExternalUrl
};

enum class ListStyle {
    Unordered,
    Ordered
};

struct SourceRange {
    uint32_t offset = 0;
    uint32_t length = 0;
};

struct LinkTarget {
    LinkKind kind = LinkKind::ExternalUrl;
    std::wstring url;
    std::wstring anchor_fragment;
};

struct InlineNode {
    InlineType type = InlineType::Text;
    std::wstring text;
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    bool code = false;
    std::optional<LinkTarget> link;
    SourceRange source;
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

struct Document {
    std::vector<BlockNode> blocks;
};
