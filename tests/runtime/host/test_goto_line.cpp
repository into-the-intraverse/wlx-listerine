#include "runtime/host/goto_line.h"

#include <doctest/doctest.h>

#include <vector>

using namespace wlx::runtime::host;

TEST_CASE("goto_handle_key: digits accumulate, Backspace pops, Enter jumps") {
    GotoPrompt p;
    p.active = true;

    CHECK(goto_handle_key(p, '4').action == GotoAction::Redraw);
    CHECK(p.buffer == L"4");
    goto_handle_key(p, '2');
    CHECK(p.buffer == L"42");
    goto_handle_key(p, VK_BACK);
    CHECK(p.buffer == L"4");
    goto_handle_key(p, '7');
    CHECK(p.buffer == L"47");

    GotoStep s = goto_handle_key(p, VK_RETURN);
    CHECK(s.action == GotoAction::Jump);
    CHECK(s.line == 47);
    CHECK(p.active == false);
    CHECK(p.buffer.empty());
}

TEST_CASE("goto_handle_key: numpad digits work; Escape closes without jumping") {
    GotoPrompt p;
    p.active = true;
    goto_handle_key(p, VK_NUMPAD9);
    CHECK(p.buffer == L"9");

    GotoStep s = goto_handle_key(p, VK_ESCAPE);
    CHECK(s.action == GotoAction::Close);
    CHECK(p.active == false);
    CHECK(p.buffer.empty());
}

TEST_CASE("goto_handle_key: Enter with empty buffer closes, does not jump") {
    GotoPrompt p;
    p.active = true;
    GotoStep s = goto_handle_key(p, VK_RETURN);
    CHECK(s.action == GotoAction::Close);
    CHECK(p.active == false);
}

TEST_CASE("goto_handle_key: buffer capped at 7 digits; non-digits ignored") {
    GotoPrompt p;
    p.active = true;
    for (int i = 0; i < 10; ++i) goto_handle_key(p, '9');
    CHECK(p.buffer.size() == 7);

    GotoStep s = goto_handle_key(p, 'X');
    CHECK(s.action == GotoAction::Ignore);
    CHECK(p.buffer.size() == 7);
}

TEST_CASE("line_scroll_target: clamps to [1,total] and to max_scroll_y") {
    std::vector<float> tops = {0.0f, 100.0f, 200.0f, 300.0f};  // 4 lines
    float max_scroll = 250.0f;

    CHECK(line_scroll_target(tops, 0, max_scroll) == doctest::Approx(0.0f));    // -> line 1
    CHECK(line_scroll_target(tops, 1, max_scroll) == doctest::Approx(0.0f));
    CHECK(line_scroll_target(tops, 3, max_scroll) == doctest::Approx(200.0f));
    CHECK(line_scroll_target(tops, 4, max_scroll) == doctest::Approx(250.0f));  // 300 clamped
    CHECK(line_scroll_target(tops, 99, max_scroll) == doctest::Approx(250.0f)); // -> line 4 clamped
    CHECK(line_scroll_target({}, 5, max_scroll) == doctest::Approx(0.0f));      // empty
}
