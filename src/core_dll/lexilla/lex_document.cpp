#include "core_dll/lexilla/lex_document.h"

#include <algorithm>

namespace wlx::core::lexilla {

namespace {

// Bytes consumed by a UTF-8 sequence given its lead byte (1 for ASCII and for
// trail/invalid bytes). Mirrors Lexilla's TestDocument helper.
const unsigned char kUtf8BytesOfLead[256] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, 4,4,4,4,4,1,1,1,1,1,1,1,1,1,1,1,
};

int unicode_from_utf8(const unsigned char* us) noexcept {
    switch (kUtf8BytesOfLead[us[0]]) {
    case 1:  return us[0];
    case 2:  return ((us[0] & 0x1F) << 6) + (us[1] & 0x3F);
    case 3:  return ((us[0] & 0x0F) << 12) + ((us[1] & 0x3F) << 6) + (us[2] & 0x3F);
    default: return ((us[0] & 0x07) << 18) + ((us[1] & 0x3F) << 12)
                  + ((us[2] & 0x3F) << 6) + (us[3] & 0x3F);
    }
}

inline bool utf8_is_trail_byte(unsigned char ch) noexcept {
    return (ch >= 0x80) && (ch < 0xC0);
}

}  // namespace

LexDocument::LexDocument(std::string_view source)
    : text_(source) {
    styles_.resize(text_.size() + 1);
    line_starts_.push_back(0);
    for (size_t pos = 0; pos < text_.size(); ++pos) {
        if (text_[pos] == '\n')
            line_starts_.push_back(static_cast<Sci_Position>(pos + 1));
    }
    if (line_starts_.back() != Length())
        line_starts_.push_back(Length());
    line_states_.resize(line_starts_.size() + 1);
    line_levels_.assign(line_starts_.size(), 0x400);
}

Sci_Position LexDocument::MaxLine() const noexcept {
    return static_cast<Sci_Position>(line_starts_.size()) - 1;
}

int SCI_METHOD LexDocument::Version() const {
    return Scintilla::dvRelease4;
}

void SCI_METHOD LexDocument::SetErrorStatus(int) {
}

Sci_Position SCI_METHOD LexDocument::Length() const {
    return static_cast<Sci_Position>(text_.size());
}

void SCI_METHOD LexDocument::GetCharRange(char* buffer, Sci_Position position,
                                          Sci_Position lengthRetrieve) const {
    if (!buffer || position < 0 || lengthRetrieve <= 0) return;
    text_.copy(buffer, static_cast<size_t>(lengthRetrieve), static_cast<size_t>(position));
}

char SCI_METHOD LexDocument::StyleAt(Sci_Position position) const {
    if (position < 0 || static_cast<size_t>(position) >= styles_.size()) return 0;
    return styles_[static_cast<size_t>(position)];
}

Sci_Position SCI_METHOD LexDocument::LineFromPosition(Sci_Position position) const {
    if (position >= Length()) return MaxLine();
    const auto it = std::lower_bound(line_starts_.begin(), line_starts_.end(), position);
    Sci_Position line = static_cast<Sci_Position>(it - line_starts_.begin());
    if (it != line_starts_.end() && *it > position) line--;
    return line;
}

Sci_Position SCI_METHOD LexDocument::LineStart(Sci_Position line) const {
    if (line < 0) return 0;
    if (line >= static_cast<Sci_Position>(line_starts_.size())) return Length();
    return line_starts_[static_cast<size_t>(line)];
}

int SCI_METHOD LexDocument::GetLevel(Sci_Position line) const {
    if (line < 0 || static_cast<size_t>(line) >= line_levels_.size()) return 0x400;
    return line_levels_[static_cast<size_t>(line)];
}

int SCI_METHOD LexDocument::SetLevel(Sci_Position line, int level) {
    if (line >= 0 && static_cast<size_t>(line) < line_levels_.size())
        line_levels_[static_cast<size_t>(line)] = level;
    return level;
}

int SCI_METHOD LexDocument::GetLineState(Sci_Position line) const {
    if (line < 0 || static_cast<size_t>(line) >= line_states_.size()) return 0;
    return line_states_[static_cast<size_t>(line)];
}

int SCI_METHOD LexDocument::SetLineState(Sci_Position line, int state) {
    if (line >= 0 && static_cast<size_t>(line) < line_states_.size())
        line_states_[static_cast<size_t>(line)] = state;
    return state;
}

void SCI_METHOD LexDocument::StartStyling(Sci_Position position) {
    styled_to_ = position;
}

bool SCI_METHOD LexDocument::SetStyleFor(Sci_Position length, char style) {
    if (length < 0 || styled_to_ < 0) return false;
    const size_t pos = static_cast<size_t>(styled_to_);
    const size_t len = static_cast<size_t>(length);
    if (pos + len > styles_.size()) return false;
    styles_.replace(pos, len, len, style);
    styled_to_ += length;
    return true;
}

bool SCI_METHOD LexDocument::SetStyles(Sci_Position length, const char* styles) {
    if (length < 0 || styled_to_ < 0 || !styles) return false;
    const size_t pos = static_cast<size_t>(styled_to_);
    const size_t len = static_cast<size_t>(length);
    if (pos + len > styles_.size()) return false;
    styles_.replace(pos, len, styles, len);
    styled_to_ += length;
    return true;
}

void SCI_METHOD LexDocument::DecorationSetCurrentIndicator(int) {
}

void SCI_METHOD LexDocument::DecorationFillRange(Sci_Position, int, Sci_Position) {
}

void SCI_METHOD LexDocument::ChangeLexerState(Sci_Position, Sci_Position) {
}

int SCI_METHOD LexDocument::CodePage() const {
    return 65001;  // UTF-8
}

bool SCI_METHOD LexDocument::IsDBCSLeadByte(char) const {
    return false;  // UTF-8 — no DBCS lead bytes
}

const char* SCI_METHOD LexDocument::BufferPointer() {
    return text_.c_str();
}

int SCI_METHOD LexDocument::GetLineIndentation(Sci_Position) {
    return 0;  // lexers use Accessor::IndentAmount, which reads via GetCharRange
}

Sci_Position SCI_METHOD LexDocument::LineEnd(Sci_Position line) const {
    if (line >= MaxLine()) return Length();
    Sci_Position position = LineStart(line + 1);
    position--;  // back over the CR or LF
    if (position > LineStart(line) &&
        text_[static_cast<size_t>(position - 1)] == '\r')
        position--;  // CR+LF: back over the CR too
    return position;
}

Sci_Position SCI_METHOD LexDocument::GetRelativePosition(Sci_Position positionStart,
                                                         Sci_Position characterOffset) const {
    Sci_Position pos = positionStart;
    if (characterOffset < 0) {
        while (characterOffset < 0) {
            if (pos <= 0) return -1;
            unsigned char previousByte =
                static_cast<unsigned char>(text_[static_cast<size_t>(pos - 1)]);
            if (previousByte < 0x80) {
                pos--;
            } else {
                while (pos > 1 && utf8_is_trail_byte(previousByte)) {
                    pos--;
                    previousByte =
                        static_cast<unsigned char>(text_[static_cast<size_t>(pos - 1)]);
                }
                pos--;
            }
            characterOffset++;
        }
        return pos;
    }
    while (characterOffset > 0) {
        Sci_Position width = 0;
        GetCharacterAndWidth(pos, &width);
        pos += width;
        characterOffset--;
    }
    return pos;
}

int SCI_METHOD LexDocument::GetCharacterAndWidth(Sci_Position position,
                                                 Sci_Position* pWidth) const {
    if (position < 0 || position >= Length()) {
        if (pWidth) *pWidth = 1;
        return '\0';
    }
    const unsigned char leadByte =
        static_cast<unsigned char>(text_[static_cast<size_t>(position)]);
    if (leadByte < 0x80) {
        if (pWidth) *pWidth = 1;
        return leadByte;
    }
    const int widthCharBytes = kUtf8BytesOfLead[leadByte];
    unsigned char charBytes[4] = {leadByte, 0, 0, 0};
    for (int b = 1; b < widthCharBytes; ++b) {
        const Sci_Position p = position + b;
        charBytes[b] = (p < Length())
            ? static_cast<unsigned char>(text_[static_cast<size_t>(p)]) : 0;
    }
    if (pWidth) *pWidth = widthCharBytes;
    return unicode_from_utf8(charBytes);
}

}  // namespace wlx::core::lexilla
