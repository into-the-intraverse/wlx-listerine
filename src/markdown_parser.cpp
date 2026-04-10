#include "markdown_parser.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstring>

// --------------- static callbacks ---------------

int MarkdownParser::cb_enter_block(MD_BLOCKTYPE type, void* detail, void* ud) {
    static_cast<MarkdownParser*>(ud)->enter_block(type, detail);
    return 0;
}

int MarkdownParser::cb_leave_block(MD_BLOCKTYPE type, void* detail, void* ud) {
    static_cast<MarkdownParser*>(ud)->leave_block(type, detail);
    return 0;
}

int MarkdownParser::cb_enter_span(MD_SPANTYPE type, void* detail, void* ud) {
    static_cast<MarkdownParser*>(ud)->enter_span(type, detail);
    return 0;
}

int MarkdownParser::cb_leave_span(MD_SPANTYPE type, void* detail, void* ud) {
    static_cast<MarkdownParser*>(ud)->leave_span(type, detail);
    return 0;
}

int MarkdownParser::cb_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* ud) {
    static_cast<MarkdownParser*>(ud)->on_text(type, text, size);
    return 0;
}

// --------------- helpers ---------------

std::wstring MarkdownParser::utf8_to_wstring(const char* text, size_t len) {
    if (len == 0) return {};
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(len), nullptr, 0);
    if (wide_len <= 0) return {};
    std::wstring result(static_cast<size_t>(wide_len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(len), &result[0], wide_len);
    return result;
}

LinkTarget MarkdownParser::classify_link(const char* href, size_t len) {
    std::string url(href, len);
    std::wstring wurl = utf8_to_wstring(href, len);

    if (!url.empty() && url[0] == '#') {
        // Internal anchor: #section
        return LinkTarget{
            LinkKind::InternalAnchor,
            L"",
            utf8_to_wstring(href + 1, len - 1)
        };
    }

    // Check for absolute URLs
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        return LinkTarget{
            LinkKind::ExternalUrl,
            wurl,
            L""
        };
    }

    // Relative doc - may contain anchor fragment
    auto hash_pos = url.find('#');
    if (hash_pos != std::string::npos) {
        return LinkTarget{
            LinkKind::RelativeDoc,
            utf8_to_wstring(href, hash_pos),
            utf8_to_wstring(href + hash_pos + 1, len - hash_pos - 1)
        };
    }

    // Plain relative doc
    return LinkTarget{
        LinkKind::RelativeDoc,
        wurl,
        L""
    };
}

void MarkdownParser::push_block(BlockType type) {
    BlockNode node;
    node.type = type;

    if (block_stack_.empty()) {
        doc_.blocks.push_back(std::move(node));
        block_stack_.push_back(&doc_.blocks.back());
    } else {
        block_stack_.back()->children.push_back(std::move(node));
        block_stack_.push_back(&block_stack_.back()->children.back());
    }
}

void MarkdownParser::pop_block() {
    if (!block_stack_.empty()) {
        block_stack_.pop_back();
    }
}

void MarkdownParser::add_inline(InlineNode node) {
    // Apply current style state
    node.bold = node.bold || (bold_depth_ > 0);
    node.italic = node.italic || (italic_depth_ > 0);
    node.strikethrough = node.strikethrough || (strikethrough_depth_ > 0);
    node.code = node.code || (code_depth_ > 0);
    if (pending_link_.has_value()) {
        node.link = pending_link_;
    }

    if (!block_stack_.empty()) {
        block_stack_.back()->inlines.push_back(std::move(node));
    }
}

// --------------- block handlers ---------------

void MarkdownParser::enter_block(MD_BLOCKTYPE type, void* detail) {
    switch (type) {
    case MD_BLOCK_DOC:
        // no-op: document is implicit
        break;

    case MD_BLOCK_H: {
        push_block(BlockType::Heading);
        auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
        block_stack_.back()->heading_level = static_cast<int>(h->level);
        break;
    }

    case MD_BLOCK_P:
        push_block(BlockType::Paragraph);
        break;

    case MD_BLOCK_UL:
        push_block(BlockType::List);
        block_stack_.back()->list_style = ListStyle::Unordered;
        break;

    case MD_BLOCK_OL: {
        push_block(BlockType::List);
        auto* ol = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
        block_stack_.back()->list_style = ListStyle::Ordered;
        block_stack_.back()->list_start = static_cast<int>(ol->start);
        break;
    }

    case MD_BLOCK_LI: {
        auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
        if (li->is_task) {
            push_block(BlockType::TaskList);
            block_stack_.back()->task_checked =
                (li->task_mark == 'x' || li->task_mark == 'X');
        } else {
            push_block(BlockType::ListItem);
        }
        break;
    }

    case MD_BLOCK_QUOTE:
        push_block(BlockType::BlockQuote);
        break;

    case MD_BLOCK_HR:
        push_block(BlockType::HorizontalRule);
        break;

    case MD_BLOCK_CODE: {
        push_block(BlockType::CodeFence);
        auto* code = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
        if (code->lang.text && code->lang.size > 0) {
            block_stack_.back()->code_language =
                utf8_to_wstring(code->lang.text, code->lang.size);
        }
        in_code_block_ = true;
        break;
    }

    case MD_BLOCK_TABLE: {
        push_block(BlockType::Table);
        auto* tbl = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
        block_stack_.back()->table_columns = static_cast<int>(tbl->col_count);
        break;
    }

    case MD_BLOCK_THEAD:
        // no-op: rows inside will be marked via TH cells
        break;

    case MD_BLOCK_TBODY:
        // no-op
        break;

    case MD_BLOCK_TR:
        push_block(BlockType::TableRow);
        break;

    case MD_BLOCK_TH: {
        push_block(BlockType::TableCell);
        block_stack_.back()->is_header = true;
        auto* td = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
        if (td->align == MD_ALIGN_CENTER) block_stack_.back()->cell_align = BlockNode::CellAlign::Center;
        else if (td->align == MD_ALIGN_RIGHT) block_stack_.back()->cell_align = BlockNode::CellAlign::Right;
        else if (td->align == MD_ALIGN_LEFT) block_stack_.back()->cell_align = BlockNode::CellAlign::Left;
        break;
    }

    case MD_BLOCK_TD: {
        push_block(BlockType::TableCell);
        auto* td = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
        if (td->align == MD_ALIGN_CENTER) block_stack_.back()->cell_align = BlockNode::CellAlign::Center;
        else if (td->align == MD_ALIGN_RIGHT) block_stack_.back()->cell_align = BlockNode::CellAlign::Right;
        else if (td->align == MD_ALIGN_LEFT) block_stack_.back()->cell_align = BlockNode::CellAlign::Left;
        break;
    }

    default:
        break;
    }
}

void MarkdownParser::leave_block(MD_BLOCKTYPE type, void* /*detail*/) {
    switch (type) {
    case MD_BLOCK_DOC:
    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
        // no-op
        break;

    case MD_BLOCK_CODE:
        in_code_block_ = false;
        pop_block();
        break;

    default:
        pop_block();
        break;
    }
}

// --------------- span handlers ---------------

void MarkdownParser::enter_span(MD_SPANTYPE type, void* detail) {
    switch (type) {
    case MD_SPAN_STRONG:
        bold_depth_++;
        break;

    case MD_SPAN_EM:
        italic_depth_++;
        break;

    case MD_SPAN_DEL:
        strikethrough_depth_++;
        break;

    case MD_SPAN_CODE:
        code_depth_++;
        break;

    case MD_SPAN_A: {
        auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
        pending_link_ = classify_link(a->href.text, a->href.size);
        break;
    }

    case MD_SPAN_IMG:
        // no-op for V1 (images deferred to V3)
        break;

    default:
        break;
    }
}

void MarkdownParser::leave_span(MD_SPANTYPE type, void* /*detail*/) {
    switch (type) {
    case MD_SPAN_STRONG:
        bold_depth_--;
        break;

    case MD_SPAN_EM:
        italic_depth_--;
        break;

    case MD_SPAN_DEL:
        strikethrough_depth_--;
        break;

    case MD_SPAN_CODE:
        code_depth_--;
        break;

    case MD_SPAN_A:
        pending_link_ = std::nullopt;
        break;

    default:
        break;
    }
}

// --------------- text handler ---------------

// Decode a common HTML entity to its character(s).
// Returns empty wstring if the entity is not recognized.
static std::wstring decode_entity(const char* text, size_t size) {
    // text includes the leading '&' and trailing ';'
    std::string entity(text, size);

    if (entity == "&amp;")    return L"&";
    if (entity == "&lt;")     return L"<";
    if (entity == "&gt;")     return L">";
    if (entity == "&quot;")   return L"\"";
    if (entity == "&apos;")   return L"'";
    if (entity == "&nbsp;")   return L"\u00A0";
    if (entity == "&copy;")   return L"\u00A9";
    if (entity == "&reg;")    return L"\u00AE";
    if (entity == "&trade;")  return L"\u2122";
    if (entity == "&mdash;")  return L"\u2014";
    if (entity == "&ndash;")  return L"\u2013";
    if (entity == "&laquo;")  return L"\u00AB";
    if (entity == "&raquo;")  return L"\u00BB";
    if (entity == "&lsquo;")  return L"\u2018";
    if (entity == "&rsquo;")  return L"\u2019";
    if (entity == "&ldquo;")  return L"\u201C";
    if (entity == "&rdquo;")  return L"\u201D";
    if (entity == "&bull;")   return L"\u2022";
    if (entity == "&hellip;") return L"\u2026";
    if (entity == "&rarr;")   return L"\u2192";
    if (entity == "&larr;")   return L"\u2190";
    if (entity == "&uarr;")   return L"\u2191";
    if (entity == "&darr;")   return L"\u2193";
    if (entity == "&times;")  return L"\u00D7";
    if (entity == "&divide;") return L"\u00F7";
    if (entity == "&plusmn;") return L"\u00B1";
    if (entity == "&infin;")  return L"\u221E";
    if (entity == "&ne;")     return L"\u2260";
    if (entity == "&le;")     return L"\u2264";
    if (entity == "&ge;")     return L"\u2265";
    if (entity == "&deg;")    return L"\u00B0";
    if (entity == "&micro;")  return L"\u00B5";
    if (entity == "&para;")   return L"\u00B6";
    if (entity == "&sect;")   return L"\u00A7";
    if (entity == "&cent;")   return L"\u00A2";
    if (entity == "&pound;")  return L"\u00A3";
    if (entity == "&euro;")   return L"\u20AC";
    if (entity == "&yen;")    return L"\u00A5";

    // Numeric entities: &#nnnn; or &#xhhhh;
    if (size >= 4 && text[0] == '&' && text[1] == '#') {
        uint32_t codepoint = 0;
        if (text[2] == 'x' || text[2] == 'X') {
            // Hex: &#xHHHH;
            for (size_t i = 3; i < size - 1; i++) {
                char c = text[i];
                codepoint <<= 4;
                if (c >= '0' && c <= '9')      codepoint += static_cast<uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f') codepoint += static_cast<uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') codepoint += static_cast<uint32_t>(c - 'A' + 10);
                else return {};  // invalid
            }
        } else {
            // Decimal: &#NNNN;
            for (size_t i = 2; i < size - 1; i++) {
                char c = text[i];
                if (c >= '0' && c <= '9')
                    codepoint = codepoint * 10 + static_cast<uint32_t>(c - '0');
                else return {};  // invalid
            }
        }

        if (codepoint == 0) return {};

        // Convert codepoint to wstring
        if (codepoint <= 0xFFFF) {
            return std::wstring(1, static_cast<wchar_t>(codepoint));
        } else if (codepoint <= 0x10FFFF) {
            // Surrogate pair for supplementary plane
            codepoint -= 0x10000;
            wchar_t hi = static_cast<wchar_t>(0xD800 + (codepoint >> 10));
            wchar_t lo = static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF));
            std::wstring result;
            result += hi;
            result += lo;
            return result;
        }
        return {};
    }

    // Unrecognized named entity: pass through as-is
    return {};
}

void MarkdownParser::on_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size) {
    switch (type) {
    case MD_TEXT_NORMAL: {
        InlineNode node;
        node.type = InlineType::Text;
        node.text = utf8_to_wstring(text, size);
        add_inline(std::move(node));
        break;
    }

    case MD_TEXT_CODE: {
        InlineNode node;
        node.type = InlineType::InlineCode;
        node.text = utf8_to_wstring(text, size);
        node.code = true;
        add_inline(std::move(node));
        break;
    }

    case MD_TEXT_SOFTBR: {
        InlineNode node;
        node.type = InlineType::SoftBreak;
        node.text = L"\n";
        add_inline(std::move(node));
        break;
    }

    case MD_TEXT_BR: {
        InlineNode node;
        node.type = InlineType::HardBreak;
        node.text = L"\n";
        add_inline(std::move(node));
        break;
    }

    case MD_TEXT_NULLCHAR:
        // skip
        break;

    case MD_TEXT_ENTITY: {
        std::wstring decoded = decode_entity(text, size);
        InlineNode node;
        node.type = InlineType::Text;
        if (!decoded.empty()) {
            node.text = decoded;
        } else {
            // Pass through unrecognized entities as-is
            node.text = utf8_to_wstring(text, size);
        }
        add_inline(std::move(node));
        break;
    }

    default: {
        // MD_TEXT_HTML, MD_TEXT_LATEXMATH, etc. - treat as normal text
        InlineNode node;
        node.type = InlineType::Text;
        node.text = utf8_to_wstring(text, size);
        add_inline(std::move(node));
        break;
    }
    }
}

// --------------- parse ---------------

Document MarkdownParser::parse(const char* markdown, size_t length) {
    // Reset state
    doc_ = Document{};
    block_stack_.clear();
    bold_depth_ = 0;
    italic_depth_ = 0;
    strikethrough_depth_ = 0;
    code_depth_ = 0;
    pending_link_ = std::nullopt;
    in_code_block_ = false;

    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_TABLES;
    parser.enter_block = cb_enter_block;
    parser.leave_block = cb_leave_block;
    parser.enter_span  = cb_enter_span;
    parser.leave_span  = cb_leave_span;
    parser.text        = cb_text;
    parser.debug_log   = nullptr;
    parser.syntax      = nullptr;

    md_parse(markdown, static_cast<MD_SIZE>(length), &parser, this);

    return std::move(doc_);
}
