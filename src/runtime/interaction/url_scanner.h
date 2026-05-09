#pragma once

#include <string_view>
#include <vector>

namespace wlx::runtime::interaction {

// One URL match within a source buffer.
// Offsets are wchar (UTF-16) code units, NOT bytes.
struct UrlMatch {
    int start = 0;  // inclusive
    int end   = 0;  // exclusive
};

// Hand-rolled URL scanner — recognizes http://, https://, ftp://, file://
// schemes (case-insensitive). Refuses to start a match inside an alphanumeric
// run (so `parsehttp://` does NOT match). Trims a trailing run of
// .,;:!?)]}> from each match (so `See https://x/y.` doesn't capture the
// trailing period and `(see https://x/y)` doesn't capture the closing paren).
//
// std::wregex is intentionally avoided: lookbehind support varies, and a
// linear scan is faster and more predictable for this shape.
std::vector<UrlMatch> scan_urls(std::wstring_view text);

}  // namespace wlx::runtime::interaction
