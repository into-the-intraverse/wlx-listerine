#include <doctest/doctest.h>
#include "runtime/layout/utf8_offset_map.h"

#include <string>

using namespace wlx::runtime::layout;

TEST_CASE("utf8_with_offsets: surrogate pair encodes as 4-byte UTF-8, offsets aligned") {
    // U+1F600 = UTF-16 pair D83D DE00 = UTF-8 F0 9F 98 80. The old per-wchar
    // encoder emitted two 3-byte CESU-8 units (invalid UTF-8 for tree-sitter)
    // and the offset table drifted by 2 bytes per emoji.
    std::wstring text = L"a\U0001F600b";
    REQUIRE(text.size() == 4);  // 'a' + high + low surrogate + 'b'

    auto m = utf8_with_offsets(text);

    REQUIRE(m.utf8.size() == 6);  // 1 + 4 + 1 bytes
    CHECK(static_cast<unsigned char>(m.utf8[1]) == 0xF0);
    CHECK(static_cast<unsigned char>(m.utf8[2]) == 0x9F);
    CHECK(static_cast<unsigned char>(m.utf8[3]) == 0x98);
    CHECK(static_cast<unsigned char>(m.utf8[4]) == 0x80);

    REQUIRE(m.wchar_to_byte.size() == text.size() + 1);  // + end sentinel
    CHECK(m.wchar_to_byte[0] == 0);  // 'a'
    CHECK(m.wchar_to_byte[1] == 1);  // high surrogate: pair start
    CHECK(m.wchar_to_byte[2] == 1);  // low surrogate shares the pair start
    CHECK(m.wchar_to_byte[3] == 5);  // 'b' — stays aligned after the emoji
    CHECK(m.wchar_to_byte[4] == 6);  // sentinel == total bytes
}

TEST_CASE("utf8_with_offsets: 1/2/3-byte code points accumulate correct offsets") {
    // 'a' (1 byte) + U+00E9 'é' (2 bytes) + U+4E2D '中' (3 bytes)
    std::wstring text = L"a\x00E9\x4E2D";
    auto m = utf8_with_offsets(text);

    REQUIRE(m.utf8.size() == 6);
    REQUIRE(m.wchar_to_byte.size() == 4);
    CHECK(m.wchar_to_byte[0] == 0);
    CHECK(m.wchar_to_byte[1] == 1);
    CHECK(m.wchar_to_byte[2] == 3);
    CHECK(m.wchar_to_byte[3] == 6);  // sentinel
}

TEST_CASE("utf8_with_offsets: lone surrogate is replaced with U+FFFD (3 bytes)") {
    std::wstring text = L"a";
    text += static_cast<wchar_t>(0xD800);  // lone high surrogate
    text += L'x';
    auto m = utf8_with_offsets(text);

    REQUIRE(m.utf8.size() == 5);  // 1 + 3 (U+FFFD) + 1
    CHECK(static_cast<unsigned char>(m.utf8[1]) == 0xEF);  // U+FFFD = EF BF BD
    CHECK(static_cast<unsigned char>(m.utf8[2]) == 0xBF);
    CHECK(static_cast<unsigned char>(m.utf8[3]) == 0xBD);
    REQUIRE(m.wchar_to_byte.size() == 4);
    CHECK(m.wchar_to_byte[2] == 4);  // 'x' after the 3-byte replacement
}

TEST_CASE("utf8_offsets matches utf8_with_offsets' table without the UTF-8 copy") {
    std::wstring text = L"// \x00E9t\x00E9 a\U0001F600b";  // "// été a<emoji>b"
    auto full = utf8_with_offsets(text);
    auto table = utf8_offsets(text);
    CHECK(table == full.wchar_to_byte);
}

TEST_CASE("byte_to_wchar: lower_bound over the table, identity on nullptr") {
    // "été": table [0, 2, 3, 5]
    auto table = utf8_offsets(L"\x00E9t\x00E9");
    REQUIRE(table.size() == 4);

    CHECK(byte_to_wchar(&table, 0) == 0);
    CHECK(byte_to_wchar(&table, 2) == 1);   // exact start of 't'
    CHECK(byte_to_wchar(&table, 1) == 1);   // mid-'é': first entry >= 1
    CHECK(byte_to_wchar(&table, 5) == 3);   // end -> sentinel index
    CHECK(byte_to_wchar(nullptr, 7) == 7);  // pure-ASCII identity fast path
}
