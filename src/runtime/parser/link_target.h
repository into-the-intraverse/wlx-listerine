#pragma once

#include <string>

namespace wlx::runtime::parser {


enum class LinkKind {
    InternalAnchor,
    RelativeDoc,
    ExternalUrl
};

struct LinkTarget {
    LinkKind kind = LinkKind::ExternalUrl;
    std::wstring url;
    std::wstring anchor_fragment;
};

}  // namespace wlx::runtime::parser
