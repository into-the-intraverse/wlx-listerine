#include "core_dll/highlighting/query_highlighter.h"
#include <algorithm>
#include <functional>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace wlx::core::highlighting {

using namespace wlx::core::colorizer;
using namespace wlx::core::theme;

// Transparent hash so the predicate regex cache can be looked up by a
// std::string_view (the tree-sitter query's pattern storage) without
// allocating a std::string on every #match? evaluation. Content-keyed (not
// pointer-keyed) so a recompiled query whose storage address is reused can't
// collide with a stale entry.
struct TransparentStringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

struct RawSpan {
    uint32_t start;
    uint32_t end;
    uint32_t pattern_index;
    uint32_t color;
    uint32_t bg_color;
    bool has_bg;
    uint8_t modifiers;
};

// Get the text for a capture within a match as a view into `source` (no
// allocation — this runs once per captured identifier under predicated
// patterns like the c grammar's `#match? @constant`).
static std::string_view capture_text(const TSQueryMatch& match, uint32_t capture_index,
                                     std::string_view source) {
    for (uint16_t i = 0; i < match.capture_count; i++) {
        if (match.captures[i].index == capture_index) {
            uint32_t start = ts_node_start_byte(match.captures[i].node);
            uint32_t end = ts_node_end_byte(match.captures[i].node);
            if (start <= end && end <= source.size()) {
                return source.substr(start, end - start);
            }
            return {};
        }
    }
    return {};
}

// Returns true if all predicates for this pattern pass for the given match.
static bool evaluate_predicates(const TSQuery* query, const TSQueryMatch& match,
                                std::string_view source) {
    uint32_t step_count = 0;
    const TSQueryPredicateStep* steps =
        ts_query_predicates_for_pattern(query, match.pattern_index, &step_count);

    if (step_count == 0) return true;

    // Cache compiled regexes across calls (heterogeneous lookup by string_view).
    // Mutex-guarded: production calls serialize through the CoreRegistry mutex,
    // but this class is dll-exported and e.g. the screenshot tool constructs
    // its own Colorizer, so concurrent callers are possible. A looked-up regex
    // stays valid after unlock — entries are never erased and unordered_map
    // references are stable across inserts.
    static std::mutex regex_cache_mu;
    static std::unordered_map<std::string, std::regex,
                              TransparentStringHash, std::equal_to<>> regex_cache;

    // Argument scratch buffer, reused across the predicates of this pattern.
    struct Arg {
        bool is_capture;
        uint32_t value_id; // capture index or string index
    };
    std::vector<Arg> args;

    uint32_t i = 0;
    while (i < step_count) {
        // Each predicate starts with a String step (the name), followed by args, ending with Done.
        if (steps[i].type != TSQueryPredicateStepTypeString) {
            // Unexpected; skip to next Done
            while (i < step_count && steps[i].type != TSQueryPredicateStepTypeDone) i++;
            if (i < step_count) i++; // skip Done
            continue;
        }

        // Get predicate name
        uint32_t name_len = 0;
        const char* name_ptr = ts_query_string_value_for_id(query, steps[i].value_id, &name_len);
        // View into TSQuery-owned storage (valid for this call); only compared
        // against string literals below, so no owning copy is needed.
        std::string_view pred_name(name_ptr, name_len);
        i++; // advance past name

        // Collect arguments until Done
        args.clear();
        while (i < step_count && steps[i].type != TSQueryPredicateStepTypeDone) {
            args.push_back({
                steps[i].type == TSQueryPredicateStepTypeCapture,
                steps[i].value_id
            });
            i++;
        }
        if (i < step_count) i++; // skip Done

        // Evaluate known predicates
        if (pred_name == "eq?" || pred_name == "not-eq?") {
            if (args.size() < 2) continue;
            if (!args[0].is_capture) continue;

            std::string_view lhs = capture_text(match, args[0].value_id, source);
            std::string_view rhs;
            if (args[1].is_capture) {
                rhs = capture_text(match, args[1].value_id, source);
            } else {
                uint32_t len = 0;
                const char* s = ts_query_string_value_for_id(query, args[1].value_id, &len);
                rhs = std::string_view(s, len);
            }

            bool equal = (lhs == rhs);
            if (pred_name == "eq?" && !equal) return false;
            if (pred_name == "not-eq?" && equal) return false;

        } else if (pred_name == "match?" || pred_name == "not-match?") {
            if (args.size() < 2) continue;
            if (!args[0].is_capture) continue;
            if (args[1].is_capture) continue; // regex must be a string literal

            std::string_view text = capture_text(match, args[0].value_id, source);

            uint32_t pat_len = 0;
            const char* pat_ptr = ts_query_string_value_for_id(query, args[1].value_id, &pat_len);
            std::string_view pattern(pat_ptr, pat_len);

            // Get or compile regex (lookup by view; allocate only on a miss).
            const std::regex* re = nullptr;
            {
                std::lock_guard<std::mutex> lk(regex_cache_mu);
                auto it = regex_cache.find(pattern);
                if (it == regex_cache.end()) {
                    try {
                        it = regex_cache.emplace(std::string(pattern),
                            std::regex(std::string(pattern),
                                       std::regex::ECMAScript | std::regex::optimize)).first;
                    } catch (const std::regex_error&) {
                        // Invalid regex — fall through with it == end()
                    }
                }
                if (it != regex_cache.end()) re = &it->second;
            }
            if (!re) continue; // invalid regex — treat predicate as passing

            bool matched = std::regex_search(text.begin(), text.end(), *re);
            if (pred_name == "match?" && !matched) return false;
            if (pred_name == "not-match?" && matched) return false;

        } else if (pred_name == "any-of?") {
            if (args.size() < 2) continue;
            if (!args[0].is_capture) continue;

            std::string_view text = capture_text(match, args[0].value_id, source);

            bool found = false;
            for (size_t a = 1; a < args.size(); a++) {
                if (args[a].is_capture) continue; // skip unexpected captures
                uint32_t len = 0;
                const char* s = ts_query_string_value_for_id(query, args[a].value_id, &len);
                if (text == std::string_view(s, len)) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        // Unknown predicates (set!, is?, is-not?, etc.) — treat as passing
    }

    return true;
}

std::vector<ResolvedStyle> QueryHighlighter::resolve_capture_styles(
    const TSQuery* query, const HelixTheme& theme)
{
    if (!query) return {};
    uint32_t capture_count = ts_query_capture_count(query);
    std::vector<ResolvedStyle> styles(capture_count);
    for (uint32_t i = 0; i < capture_count; i++) {
        uint32_t name_len = 0;
        const char* name = ts_query_capture_name_for_id(query, i, &name_len);
        std::string scope(name, name_len);
        if (auto style = theme.resolve(scope)) styles[i] = *style;
    }
    return styles;
}

std::vector<ColorSpan> QueryHighlighter::highlight(
    const TSTree* tree,
    const TSQuery* query,
    const HelixTheme& theme,
    std::string_view source,
    uint32_t range_start,
    uint32_t range_end)
{
    if (!tree || !query) return {};
    return highlight(tree, query, resolve_capture_styles(query, theme), source,
                     range_start, range_end);
}

std::vector<ColorSpan> QueryHighlighter::highlight(
    const TSTree* tree,
    const TSQuery* query,
    const std::vector<ResolvedStyle>& capture_styles,
    std::string_view source,
    uint32_t range_start,
    uint32_t range_end)
{
    if (!tree || !query) return {};

    // Execute query
    TSQueryCursor* cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));

    // Viewport scoping: when a non-empty range is given, limit query iteration to
    // captures intersecting [range_start, range_end). The parse is whole-file, so
    // multi-line constructs that begin outside the range still resolve (the parse
    // tree is complete; only query iteration is scoped). Default (end<=start)
    // leaves the cursor full-document.
    if (range_end > range_start)
        ts_query_cursor_set_byte_range(cursor, range_start, range_end);

    // Collect raw spans
    std::vector<RawSpan> raw;
    raw.reserve(256);  // capacity hint: skip the early geometric-growth churn
    // Predicate verdicts are per-MATCH, but next_capture yields a match once
    // per capture — memoize by match.id so each match's predicates evaluate
    // once instead of once per capture. Only predicated patterns enter the
    // map, keeping it small on predicate-free grammars.
    std::unordered_map<uint32_t, bool> predicate_memo;
    TSQueryMatch match;
    uint32_t capture_index;
    while (ts_query_cursor_next_capture(cursor, &match, &capture_index)) {
        // Evaluate predicates — skip this capture if any predicate fails
        uint32_t step_count = 0;
        ts_query_predicates_for_pattern(query, match.pattern_index, &step_count);
        if (step_count != 0) {
            auto [it, inserted] = predicate_memo.try_emplace(match.id, false);
            if (inserted) it->second = evaluate_predicates(query, match, source);
            if (!it->second) continue;
        }

        const TSQueryCapture& cap = match.captures[capture_index];
        uint32_t start = ts_node_start_byte(cap.node);
        uint32_t end = ts_node_end_byte(cap.node);
        if (cap.index >= capture_styles.size()) continue;
        const auto& cs = capture_styles[cap.index];
        if (!cs.has_fg && !cs.has_bg && cs.modifiers == 0) continue;
        // A background-only style — (ERROR)/(MISSING) @diagnostic.error — on a
        // CONTAINER node (one with named children) wraps validly-parsed subtrees.
        // Emitting its background would paint every gap between the foreground
        // syntax spans it covers red in the flatten below. tree-sitter is a
        // fuzzy, preprocessor-unaware parser: on macro-heavy but VALID C/C++
        // (e.g. sqlite3.c's SQLITE_API declarations) it can mark the whole-file
        // root as one ERROR node; without this guard that single span turns the
        // entire file red and erases all coloring. Skip container error
        // backgrounds; genuine leaf errors (no named children) still get their
        // marker.
        if (cs.has_bg && !cs.has_fg && ts_node_named_child_count(cap.node) > 0)
            continue;
        raw.push_back({start, end, match.pattern_index, cs.fg, cs.bg, cs.has_bg, cs.modifiers});
    }

    ts_query_cursor_delete(cursor);

    // Sort by start asc, then end desc so a container sorts before the spans
    // nested inside it, then pattern_index desc (later patterns win when the
    // range is identical).
    std::sort(raw.begin(), raw.end(), [](const RawSpan& a, const RawSpan& b) {
        if (a.start != b.start) return a.start < b.start;
        if (a.end != b.end) return a.end > b.end;
        return a.pattern_index > b.pattern_index;
    });

    // Flatten with an active-span stack (outermost -> innermost): a nested
    // capture (e.g. escape_sequence inside a string_literal) splits its
    // container into prefix + inner + suffix instead of being swallowed by it.
    // Captures are node ranges, so they nest or are disjoint; spans with an
    // identical range keep the later-pattern-wins rule (the winner sorts
    // first, duplicates are dropped).
    std::vector<ColorSpan> result;
    std::vector<RawSpan> active;
    uint32_t emitted_until = 0;
    auto emit = [&](const RawSpan& s, uint32_t to) {
        if (emitted_until < to)
            result.push_back({emitted_until, to - emitted_until, s.color,
                              s.bg_color, s.has_bg, s.modifiers});
        emitted_until = std::max(emitted_until, to);
    };
    for (const auto& span : raw) {
        // Close every active span that ends at or before this one, emitting
        // its uncovered tail.
        while (!active.empty() && active.back().end <= span.start) {
            emit(active.back(), active.back().end);
            active.pop_back();
        }
        if (!active.empty()) {
            if (active.back().start == span.start && active.back().end == span.end)
                continue;  // identical range: the earlier (winning) span keeps it
            emit(active.back(), span.start);  // container prefix before the inner span
        }
        emitted_until = std::max(emitted_until, span.start);
        active.push_back(span);
    }
    while (!active.empty()) {
        emit(active.back(), active.back().end);
        active.pop_back();
    }

    return result;
}

}  // namespace wlx::core::highlighting
