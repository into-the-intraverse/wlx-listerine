#include "tokenizer.h"
#include <algorithm>

static void collect_leaves(TSNode node, const std::string& source,
                           std::vector<TokenSpan>& out) {
    uint32_t child_count = ts_node_child_count(node);

    if (child_count == 0) {
        TokenSpan span;
        span.start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        span.length = end - span.start;
        span.node_type = ts_node_type(node);
        if (span.length > 0)
            out.push_back(span);
        return;
    }

    bool all_anonymous = true;
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_is_named(child)) {
            all_anonymous = false;
            break;
        }
    }

    if (ts_node_is_named(node) && all_anonymous) {
        TokenSpan span;
        span.start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        span.length = end - span.start;
        span.node_type = ts_node_type(node);
        if (span.length > 0)
            out.push_back(span);
        return;
    }

    for (uint32_t i = 0; i < child_count; i++) {
        collect_leaves(ts_node_child(node, i), source, out);
    }
}

std::vector<TokenSpan> Tokenizer::tokenize(const TSLanguage* grammar,
                                             const std::string& source) {
    if (!grammar || source.empty())
        return {};

    TSParser* parser = ts_parser_new();
    if (!parser) return {};

    if (!ts_parser_set_language(parser, grammar)) {
        ts_parser_delete(parser);
        return {};
    }

    TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                           source.c_str(),
                                           static_cast<uint32_t>(source.size()));
    if (!tree) {
        ts_parser_delete(parser);
        return {};
    }

    TSNode root = ts_tree_root_node(tree);
    std::vector<TokenSpan> spans;
    collect_leaves(root, source, spans);

    std::sort(spans.begin(), spans.end(),
              [](const TokenSpan& a, const TokenSpan& b) {
                  return a.start < b.start;
              });

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return spans;
}
