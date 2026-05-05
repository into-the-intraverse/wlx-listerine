#pragma once

#include "runtime/parser/link_target.h"
#include "runtime/parser/source_range.h"

#include <optional>
#include <string>

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
