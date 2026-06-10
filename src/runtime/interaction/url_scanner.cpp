#include "runtime/interaction/url_scanner.h"

#include <cwctype>
#include <string_view>

namespace wlx::runtime::interaction {

namespace {

// file:// is deliberately absent: auto-linking it from untrusted viewed text
// would let one click launch local/UNC executables via ShellExecuteW.
constexpr std::wstring_view kSchemes[] = {
    L"https://",
    L"http://",
    L"ftp://",
};

bool ascii_iequals(wchar_t a, wchar_t b) {
    if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a - L'A' + L'a');
    if (b >= L'A' && b <= L'Z') b = static_cast<wchar_t>(b - L'A' + L'a');
    return a == b;
}

// Returns the matched scheme length (including '://') if `text` at `i` begins
// with a known scheme, else 0.
int match_scheme(std::wstring_view text, int i) {
    for (auto& scheme : kSchemes) {
        if (i + static_cast<int>(scheme.size()) > static_cast<int>(text.size()))
            continue;
        bool ok = true;
        for (int k = 0; k < static_cast<int>(scheme.size()); ++k) {
            if (!ascii_iequals(text[i + k], scheme[k])) { ok = false; break; }
        }
        if (ok) return static_cast<int>(scheme.size());
    }
    return 0;
}

// True if the char ends a URL body — whitespace or one of the brackets/quotes
// that conventionally bound a URL literal.
bool is_url_terminator(wchar_t c) {
    if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') return true;
    switch (c) {
        case L'<': case L'>': case L'"': case L'`':
        case L'{': case L'}': case L'|': case L'\\':
        case L'^':
            return true;
        default:
            return false;
    }
}

bool is_trailing_punct(wchar_t c) {
    switch (c) {
        case L'.': case L',': case L';': case L':':
        case L'!': case L'?':
        case L')': case L']': case L'}': case L'>':
            return true;
        default:
            return false;
    }
}

}  // namespace

std::vector<UrlMatch> scan_urls(std::wstring_view text) {
    std::vector<UrlMatch> out;
    int n = static_cast<int>(text.size());
    int i = 0;

    while (i < n) {
        // Cheap pre-filter: only scheme prefixes start with h or f.
        wchar_t c0 = text[i];
        if (c0 != L'h' && c0 != L'H' && c0 != L'f' && c0 != L'F') {
            ++i;
            continue;
        }

        // Boundary check: previous char must not be alphanumeric.
        if (i > 0) {
            wchar_t prev = text[i - 1];
            if (iswalnum(prev)) { ++i; continue; }
        }

        int scheme_len = match_scheme(text, i);
        if (scheme_len == 0) { ++i; continue; }

        // Body: non-terminator characters.
        int j = i + scheme_len;
        while (j < n && !is_url_terminator(text[j])) ++j;

        // Refuse degenerate matches (just the scheme with no body).
        if (j == i + scheme_len) { i = j; continue; }

        // Trim trailing punctuation. A ')' is kept when it closes an
        // unmatched '(' earlier in the body — wiki URLs like
        // .../Tree_(data_structure) must keep their final paren.
        int end = j;
        while (end > i + scheme_len && is_trailing_punct(text[end - 1])) {
            if (text[end - 1] == L')') {
                int balance = 0;
                for (int k = i + scheme_len; k < end - 1; ++k) {
                    if (text[k] == L'(') ++balance;
                    else if (text[k] == L')') --balance;
                }
                if (balance > 0) break;
            }
            --end;
        }

        // Refuse if trim erased the body.
        if (end == i + scheme_len) { i = j; continue; }

        out.push_back({i, end});
        i = j;  // continue past whatever raw body we consumed (incl. trimmed punct)
    }

    return out;
}

}  // namespace wlx::runtime::interaction
