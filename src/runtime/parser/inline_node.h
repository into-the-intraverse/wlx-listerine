#pragma once

#include "runtime/parser/link_target.h"

#include <optional>
#include <string>

namespace wlx::runtime::parser {


enum class InlineType {
    Text,
    InlineCode,
    SoftBreak,
    HardBreak
};

struct InlineNode {
    InlineType type = InlineType::Text;
    std::wstring text;
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    bool code = false;
    std::optional<LinkTarget> link;
};

}  // namespace wlx::runtime::parser
