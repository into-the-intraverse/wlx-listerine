#pragma once

#include "runtime/parser/document_model.h"
#include <md4c.h>
#include <string>
#include <vector>
#include <optional>

class MarkdownParser {
public:
    Document parse(const char* markdown, size_t length);
    static uint32_t parser_version() { return 1; }

private:
    // Static md4c callbacks
    static int cb_enter_block(MD_BLOCKTYPE type, void* detail, void* ud);
    static int cb_leave_block(MD_BLOCKTYPE type, void* detail, void* ud);
    static int cb_enter_span(MD_SPANTYPE type, void* detail, void* ud);
    static int cb_leave_span(MD_SPANTYPE type, void* detail, void* ud);
    static int cb_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* ud);

    // Instance handlers
    void enter_block(MD_BLOCKTYPE type, void* detail);
    void leave_block(MD_BLOCKTYPE type, void* detail);
    void enter_span(MD_SPANTYPE type, void* detail);
    void leave_span(MD_SPANTYPE type, void* detail);
    void on_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size);

    // Helpers
    std::wstring utf8_to_wstring(const char* text, size_t len);
    LinkTarget classify_link(const char* href, size_t len);
    void push_block(BlockType type);
    void pop_block();
    void add_inline(InlineNode node);

    // Build state
    Document doc_;
    std::vector<BlockNode*> block_stack_;

    // Inline style accumulator (depth counters for nesting)
    int bold_depth_ = 0;
    int italic_depth_ = 0;
    int strikethrough_depth_ = 0;
    int code_depth_ = 0;
    std::optional<LinkTarget> pending_link_;

    // Code block state
    bool in_code_block_ = false;
};
