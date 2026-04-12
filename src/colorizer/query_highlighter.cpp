#include "query_highlighter.h"
#include "scope.h"
#include <algorithm>
#include <regex>
#include <string>
#include <unordered_map>

struct RawSpan {
    uint32_t start;
    uint32_t end;
    uint32_t pattern_index;
    Scope scope;
};

// Get the text for a capture within a match, given the source string.
static std::string capture_text(const TSQueryMatch& match, uint32_t capture_index,
                                const std::string& source) {
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
                                const std::string& source) {
    uint32_t step_count = 0;
    const TSQueryPredicateStep* steps =
        ts_query_predicates_for_pattern(query, match.pattern_index, &step_count);

    if (step_count == 0) return true;

    // Cache compiled regexes across calls
    static std::unordered_map<std::string, std::regex> regex_cache;

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
        std::string pred_name(name_ptr, name_len);
        i++; // advance past name

        // Collect arguments until Done
        struct Arg {
            bool is_capture;
            uint32_t value_id; // capture index or string index
        };
        std::vector<Arg> args;
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

            std::string lhs = capture_text(match, args[0].value_id, source);
            std::string rhs;
            if (args[1].is_capture) {
                rhs = capture_text(match, args[1].value_id, source);
            } else {
                uint32_t len = 0;
                const char* s = ts_query_string_value_for_id(query, args[1].value_id, &len);
                rhs.assign(s, len);
            }

            bool equal = (lhs == rhs);
            if (pred_name == "eq?" && !equal) return false;
            if (pred_name == "not-eq?" && equal) return false;

        } else if (pred_name == "match?" || pred_name == "not-match?") {
            if (args.size() < 2) continue;
            if (!args[0].is_capture) continue;
            if (args[1].is_capture) continue; // regex must be a string literal

            std::string text = capture_text(match, args[0].value_id, source);

            uint32_t pat_len = 0;
            const char* pat_ptr = ts_query_string_value_for_id(query, args[1].value_id, &pat_len);
            std::string pattern(pat_ptr, pat_len);

            // Get or compile regex
            auto it = regex_cache.find(pattern);
            if (it == regex_cache.end()) {
                try {
                    it = regex_cache.emplace(pattern,
                        std::regex(pattern, std::regex::ECMAScript | std::regex::optimize)).first;
                } catch (const std::regex_error&) {
                    // Invalid regex — treat predicate as passing
                    continue;
                }
            }

            bool matched = std::regex_search(text, it->second);
            if (pred_name == "match?" && !matched) return false;
            if (pred_name == "not-match?" && matched) return false;

        } else if (pred_name == "any-of?") {
            if (args.size() < 2) continue;
            if (!args[0].is_capture) continue;

            std::string text = capture_text(match, args[0].value_id, source);

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

std::vector<ColorSpan> QueryHighlighter::highlight(
    const TSTree* tree,
    const TSQuery* query,
    const SyntaxPalette& palette,
    const std::string& source)
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
        // Evaluate predicates — skip this capture if any predicate fails
        if (!evaluate_predicates(query, match, source)) continue;

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
