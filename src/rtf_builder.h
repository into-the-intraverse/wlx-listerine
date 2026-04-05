#pragma once

#include "config.h"
#include <md4c.h>
#include <string>
#include <vector>

struct LinkInfo {
    size_t char_start;
    size_t char_end;
    std::string url;
};

class RtfBuilder {
public:
    RtfBuilder(const Config& config, bool dark_mode);
    std::string build(const char* markdown, size_t length);
    const std::vector<LinkInfo>& links() const { return links_; }

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

    // RTF helpers
    void emit(const char* s);
    void emit(const std::string& s);
    void emit_text(const char* text, size_t len); // escapes + counts chars
    void emit_rtf_color(std::string& out, uint32_t rgb);
    std::string preamble();
    void emit_default_fmt();

    // config_ must be declared before colors_ so it is constructed first;
    // colors_ holds a reference into config_.light or config_.dark.
    Config config_;
    const ColorScheme& colors_;
    std::string rtf_;
    size_t char_count_ = 0;

    // Block state
    int list_nesting_ = 0;
    std::vector<bool> list_ordered_;
    std::vector<int> list_counter_;
    bool in_code_block_ = false;
    bool in_table_ = false;
    bool in_table_header_ = false;
    int table_col_count_ = 0;
    int table_col_ = 0;

    // Link tracking
    std::vector<LinkInfo> links_;
    std::string pending_link_url_;
    size_t pending_link_start_ = 0;

    // Image state
    bool in_image_ = false;
    std::string pending_image_alt_;
};
