#include "query_highlighter.h"
#include "scope.h"
#include <algorithm>

struct RawSpan {
    uint32_t start;
    uint32_t end;
    uint32_t pattern_index;
    Scope scope;
};

std::vector<ColorSpan> QueryHighlighter::highlight(
    const TSTree* tree,
    const TSQuery* query,
    const SyntaxPalette& palette)
{
    if (!tree || !query) return {};

    // Build capture_index -> Scope lookup
    uint32_t capture_count = ts_query_capture_count(query);
    std::vector<Scope> capture_scopes(capture_count, Scope::Plain);
    for (uint32_t i = 0; i < capture_count; i++) {
        uint32_t name_len = 0;
        const char* name = ts_query_capture_name_for_id(query, i, &name_len);
        capture_scopes[i] = capture_to_scope(std::string_view(name, name_len));
    }

    // Execute query
    TSQueryCursor* cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));

    // Collect raw spans
    std::vector<RawSpan> raw;
    TSQueryMatch match;
    uint32_t capture_index;
    while (ts_query_cursor_next_capture(cursor, &match, &capture_index)) {
        const TSQueryCapture& cap = match.captures[capture_index];
        uint32_t start = ts_node_start_byte(cap.node);
        uint32_t end = ts_node_end_byte(cap.node);
        Scope scope = (cap.index < capture_count) ? capture_scopes[cap.index] : Scope::Plain;
        if (scope == Scope::Plain) continue;
        raw.push_back({start, end, match.pattern_index, scope});
    }

    ts_query_cursor_delete(cursor);

    // Sort by start position, then by pattern_index descending (later patterns win)
    std::sort(raw.begin(), raw.end(), [](const RawSpan& a, const RawSpan& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.pattern_index > b.pattern_index;
    });

    // Flatten overlapping spans: sweep left-to-right
    std::vector<ColorSpan> result;
    uint32_t covered_until = 0;
    for (const auto& span : raw) {
        uint32_t eff_start = std::max(span.start, covered_until);
        if (eff_start >= span.end) continue;
        uint32_t color = scope_to_color(span.scope, palette);
        result.push_back({eff_start, span.end - eff_start, color});
        covered_until = span.end;
    }

    return result;
}
