#pragma once

#include <string>

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
