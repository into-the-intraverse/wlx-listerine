#include "rtf_builder.h"
#include <cstdio>

// --------------- helpers ---------------

void RtfBuilder::emit(const char* s) { rtf_ += s; }
void RtfBuilder::emit(const std::string& s) { rtf_ += s; }

void RtfBuilder::emit_rtf_color(std::string& out, uint32_t rgb) {
    char buf[48];
    snprintf(buf, sizeof(buf), "\\red%u\\green%u\\blue%u;",
             (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    out += buf;
}

void RtfBuilder::emit_text(const char* text, size_t len) {
    for (size_t i = 0; i < len;) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (c == '\\' || c == '{' || c == '}') {
                rtf_ += '\\';
                rtf_ += static_cast<char>(c);
            } else if (c == '\n') {
                emit("\\line\n");
            } else {
                rtf_ += static_cast<char>(c);
            }
            char_count_++;
            i++;
        } else {
            // Decode UTF-8 -> code point -> RTF \uN?
            uint32_t cp = 0;
            int bytes = 0;
            if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; bytes = 2; }
            else if ((c & 0xF0) == 0xE0)  { cp = c & 0x0F; bytes = 3; }
            else if ((c & 0xF8) == 0xF0)  { cp = c & 0x07; bytes = 4; }
            else { i++; continue; }
            for (int j = 1; j < bytes && (i + j) < len; j++)
                cp = (cp << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3F);
            char buf[24];
            if (cp <= 0xFFFF) {
                snprintf(buf, sizeof(buf), "\\u%d?", static_cast<int16_t>(cp));
                rtf_ += buf;
                char_count_++;
            } else {
                // Surrogate pair for supplementary plane
                cp -= 0x10000;
                int16_t hi = static_cast<int16_t>(0xD800 + (cp >> 10));
                int16_t lo = static_cast<int16_t>(0xDC00 + (cp & 0x3FF));
                snprintf(buf, sizeof(buf), "\\u%d?\\u%d?", hi, lo);
                rtf_ += buf;
                char_count_ += 2;
            }
            i += bytes;
        }
    }
}

void RtfBuilder::emit_default_fmt() {
    char buf[32];
    snprintf(buf, sizeof(buf), "\\f0\\fs%d\\cf1", config_.body_size * 2);
    emit(buf);
}

std::string RtfBuilder::preamble() {
    std::string p = "{\\rtf1\\ansi\\deff0\n";

    p += "{\\fonttbl";
    p += "{\\f0\\fswiss " + config_.body_font + ";}";
    p += "{\\f1\\fmodern " + config_.code_font + ";}";
    p += "}\n";

    p += "{\\colortbl;";
    emit_rtf_color(p, colors_.text);           // cf1
    emit_rtf_color(p, colors_.heading);        // cf2
    emit_rtf_color(p, colors_.link);           // cf3
    emit_rtf_color(p, colors_.blockquote);     // cf4
    emit_rtf_color(p, colors_.code_background);// cf5
    p += "}\n";

    char buf[32];
    snprintf(buf, sizeof(buf), "\\f0\\fs%d\\cf1\n", config_.body_size * 2);
    p += buf;

    return p;
}

// --------------- static callbacks ---------------

int RtfBuilder::cb_enter_block(MD_BLOCKTYPE t, void* d, void* ud) {
    static_cast<RtfBuilder*>(ud)->enter_block(t, d); return 0;
}
int RtfBuilder::cb_leave_block(MD_BLOCKTYPE t, void* d, void* ud) {
    static_cast<RtfBuilder*>(ud)->leave_block(t, d); return 0;
}
int RtfBuilder::cb_enter_span(MD_SPANTYPE t, void* d, void* ud) {
    static_cast<RtfBuilder*>(ud)->enter_span(t, d); return 0;
}
int RtfBuilder::cb_leave_span(MD_SPANTYPE t, void* d, void* ud) {
    static_cast<RtfBuilder*>(ud)->leave_span(t, d); return 0;
}
int RtfBuilder::cb_text(MD_TEXTTYPE t, const MD_CHAR* text, MD_SIZE sz, void* ud) {
    static_cast<RtfBuilder*>(ud)->on_text(t, text, sz); return 0;
}

// --------------- block handlers ---------------

static const int kHeadingSizes[] = {56, 48, 40, 32, 28, 24}; // H1-H6 in half-points

void RtfBuilder::enter_block(MD_BLOCKTYPE type, void* detail) {
    switch (type) {
    case MD_BLOCK_DOC:
        break;

    case MD_BLOCK_H: {
        auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
        int level = h->level; // 1-6
        int fs = (level >= 1 && level <= 6) ? kHeadingSizes[level - 1] : 24;
        emit("\\pard\\sb200\\sa100");
        if (level <= 2)
            emit("\\brdrb\\brdrs\\brdrw10\\brdrcf1");
        char buf[64];
        snprintf(buf, sizeof(buf), "\\f0\\fs%d\\b\\cf2 ", fs);
        emit(buf);
        break;
    }

    case MD_BLOCK_P:
        if (in_table_) break;
        emit("\\pard\\sa200 ");
        emit_default_fmt();
        emit(" ");
        break;

    case MD_BLOCK_UL:
        list_ordered_.push_back(false);
        list_counter_.push_back(0);
        list_nesting_++;
        break;

    case MD_BLOCK_OL: {
        auto* ol = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
        list_ordered_.push_back(true);
        list_counter_.push_back(static_cast<int>(ol->start));
        list_nesting_++;
        break;
    }

    case MD_BLOCK_LI: {
        int indent = list_nesting_ * 360;
        char buf[128];
        snprintf(buf, sizeof(buf), "\\pard\\fi-360\\li%d\\sa100 ", indent);
        emit(buf);
        emit_default_fmt();

        auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
        if (li->is_task) {
            if (li->task_mark == 'x' || li->task_mark == 'X') {
                emit(" \\u9745? ");  // ballot box with check
            } else {
                emit(" \\u9744? ");  // ballot box
            }
            char_count_ += 2;
        } else if (!list_ordered_.empty() && list_ordered_.back()) {
            int& counter = list_counter_.back();
            char nbuf[16];
            int nlen = snprintf(nbuf, sizeof(nbuf), " %d. ", counter);
            emit(nbuf);
            char_count_ += nlen - 1; // exclude leading space that's formatting
            counter++;
        } else {
            emit(" \\u8226  ");  // bullet
            char_count_ += 2;
        }
        break;
    }

    case MD_BLOCK_CODE: {
        char buf[64];
        snprintf(buf, sizeof(buf), "\\pard\\li200\\ri200\\sa100\\f1\\fs%d\\highlight5\\cf1 ",
                 config_.code_size * 2);
        emit(buf);
        in_code_block_ = true;
        break;
    }

    case MD_BLOCK_QUOTE:
        emit("\\pard\\li720\\brdrbar\\brdrs\\brdrw20\\brdrcf4\\sa200 ");
        emit_default_fmt();
        emit("\\cf4 ");
        break;

    case MD_BLOCK_HR:
        emit("\\pard\\brdrb\\brdrs\\brdrw10\\brdrcf1\\sa200 \\par\n");
        char_count_++;
        break;

    case MD_BLOCK_TABLE: {
        auto* tbl = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
        table_col_count_ = static_cast<int>(tbl->col_count);
        in_table_ = true;
        break;
    }

    case MD_BLOCK_THEAD:
        in_table_header_ = true;
        break;

    case MD_BLOCK_TBODY:
        in_table_header_ = false;
        break;

    case MD_BLOCK_TR: {
        table_col_ = 0;
        emit("\\trowd\\trgaph108");
        int col_width = 9000 / (table_col_count_ > 0 ? table_col_count_ : 1);
        char buf[32];
        for (int i = 0; i < table_col_count_; i++) {
            snprintf(buf, sizeof(buf), "\\cellx%d", col_width * (i + 1));
            emit(buf);
        }
        emit("\n");
        break;
    }

    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        emit("\\pard\\intbl ");
        emit_default_fmt();
        if (in_table_header_) emit("\\b");
        emit(" ");
        break;

    default:
        break;
    }
}

void RtfBuilder::leave_block(MD_BLOCKTYPE type, void* detail) {
    switch (type) {
    case MD_BLOCK_DOC:
        break;

    case MD_BLOCK_H:
        emit("\\b0\\par\n");
        char_count_++;
        break;

    case MD_BLOCK_P:
        if (in_table_) break;
        emit("\\par\n");
        char_count_++;
        break;

    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        if (!list_ordered_.empty()) list_ordered_.pop_back();
        if (!list_counter_.empty()) list_counter_.pop_back();
        list_nesting_--;
        break;

    case MD_BLOCK_LI:
        emit("\\par\n");
        char_count_++;
        break;

    case MD_BLOCK_CODE:
        emit("\\highlight0\\par\n");
        char_count_++;
        in_code_block_ = false;
        break;

    case MD_BLOCK_QUOTE:
        emit("\\par\n");
        char_count_++;
        break;

    case MD_BLOCK_HR:
        break;

    case MD_BLOCK_TH:
    case MD_BLOCK_TD:
        emit("\\cell\n");
        char_count_++;
        table_col_++;
        break;

    case MD_BLOCK_TR:
        emit("\\row\n");
        char_count_++;
        break;

    case MD_BLOCK_TABLE:
        in_table_ = false;
        break;

    case MD_BLOCK_THEAD:
    case MD_BLOCK_TBODY:
        break;

    default:
        break;
    }
}

// --------------- span handlers (stubs — Task 5 fills these in) ---------------

void RtfBuilder::enter_span(MD_SPANTYPE type, void* detail) {}
void RtfBuilder::leave_span(MD_SPANTYPE type, void* detail) {}

// --------------- text handler ---------------

void RtfBuilder::on_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size) {
    if (in_image_) {
        pending_image_alt_.append(text, size);
        return;
    }
    switch (type) {
    case MD_TEXT_NORMAL:
    case MD_TEXT_CODE:
        emit_text(text, size);
        break;
    case MD_TEXT_BR:
        emit("\\line\n");
        char_count_++;
        break;
    case MD_TEXT_SOFTBR:
        emit(" ");
        char_count_++;
        break;
    case MD_TEXT_ENTITY:
        emit_text(text, size);
        break;
    default:
        emit_text(text, size);
        break;
    }
}

// --------------- build ---------------

RtfBuilder::RtfBuilder(const Config& config, bool dark_mode)
    : config_(config)
    , colors_(dark_mode ? config_.dark : config_.light) {}

std::string RtfBuilder::build(const char* markdown, size_t length) {
    rtf_.clear();
    rtf_.reserve(length * 3);
    char_count_ = 0;
    links_.clear();
    list_nesting_ = 0;
    list_ordered_.clear();
    list_counter_.clear();
    in_code_block_ = false;
    in_table_ = false;
    in_table_header_ = false;
    in_image_ = false;

    rtf_ = preamble();

    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_NOHTML;
    parser.enter_block = cb_enter_block;
    parser.leave_block = cb_leave_block;
    parser.enter_span  = cb_enter_span;
    parser.leave_span  = cb_leave_span;
    parser.text        = cb_text;

    md_parse(markdown, static_cast<MD_SIZE>(length), &parser, this);

    rtf_ += "}";
    return rtf_;
}
