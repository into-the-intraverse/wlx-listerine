#pragma once

#include <string>
#include <string_view>
#include <vector>

// Scintilla's lexer interface. Third-party header — wrap so its declarations
// can't trip the first-party /W4 /WX build.
#if defined(_MSC_VER)
#  pragma warning(push, 3)
#endif
#include "ILexer.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

namespace wlx::core::lexilla {

// Minimal in-memory Scintilla::IDocument over a UTF-8 byte buffer, so Lexilla
// lexers can run headlessly (no editor, no Scintilla GUI). Adapted from
// Lexilla's TestDocument (Copyright 2019 Neil Hodgson; HPND license — see the
// vendored Lexilla License.txt). Folding/decoration entry points are stubbed;
// only the lexing surface is real. The byte buffer is copied in so the lexer's
// line index and the style buffer share one lifetime.
class LexDocument final : public Scintilla::IDocument {
public:
    explicit LexDocument(std::string_view source);

    // One style byte per source byte, valid after a lexer Lex() pass.
    std::string_view styles() const { return styles_; }

    int SCI_METHOD Version() const override;
    void SCI_METHOD SetErrorStatus(int status) override;
    Sci_Position SCI_METHOD Length() const override;
    void SCI_METHOD GetCharRange(char* buffer, Sci_Position position,
                                 Sci_Position lengthRetrieve) const override;
    char SCI_METHOD StyleAt(Sci_Position position) const override;
    Sci_Position SCI_METHOD LineFromPosition(Sci_Position position) const override;
    Sci_Position SCI_METHOD LineStart(Sci_Position line) const override;
    int SCI_METHOD GetLevel(Sci_Position line) const override;
    int SCI_METHOD SetLevel(Sci_Position line, int level) override;
    int SCI_METHOD GetLineState(Sci_Position line) const override;
    int SCI_METHOD SetLineState(Sci_Position line, int state) override;
    void SCI_METHOD StartStyling(Sci_Position position) override;
    bool SCI_METHOD SetStyleFor(Sci_Position length, char style) override;
    bool SCI_METHOD SetStyles(Sci_Position length, const char* styles) override;
    void SCI_METHOD DecorationSetCurrentIndicator(int indicator) override;
    void SCI_METHOD DecorationFillRange(Sci_Position position, int value,
                                        Sci_Position fillLength) override;
    void SCI_METHOD ChangeLexerState(Sci_Position start, Sci_Position end) override;
    int SCI_METHOD CodePage() const override;
    bool SCI_METHOD IsDBCSLeadByte(char ch) const override;
    const char* SCI_METHOD BufferPointer() override;
    int SCI_METHOD GetLineIndentation(Sci_Position line) override;
    Sci_Position SCI_METHOD LineEnd(Sci_Position line) const override;
    Sci_Position SCI_METHOD GetRelativePosition(Sci_Position positionStart,
                                                Sci_Position characterOffset) const override;
    int SCI_METHOD GetCharacterAndWidth(Sci_Position position,
                                        Sci_Position* pWidth) const override;

private:
    Sci_Position MaxLine() const noexcept;

    std::string text_;
    std::string styles_;
    std::vector<Sci_Position> line_starts_;
    std::vector<int> line_states_;
    std::vector<int> line_levels_;
    Sci_Position styled_to_ = 0;
};

}  // namespace wlx::core::lexilla
