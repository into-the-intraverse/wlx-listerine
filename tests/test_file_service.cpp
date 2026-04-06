#include <doctest/doctest.h>
#include "file_service.h"
#include <fstream>
#include <cstdio>

TEST_CASE("read existing markdown file") {
    FileService fs;
    auto result = fs.read(L"test_data/sample.md");

    REQUIRE(result.has_value());
    CHECK_FALSE(result->raw_utf8.empty());
    CHECK_FALSE(result->text.empty());
    CHECK(result->identity.path == L"test_data/sample.md");
    CHECK(result->identity.size > 0);
    CHECK(result->identity.mtime > 0);
}

TEST_CASE("read nonexistent file returns nullopt") {
    FileService fs;
    auto result = fs.read(L"test_data/does_not_exist.md");

    CHECK_FALSE(result.has_value());
}

TEST_CASE("normalize line endings via file read") {
    const char* path = "test_data/temp_test_crlf.txt";

    // Write a file with \r\n line endings using binary mode
    {
        std::ofstream f(path, std::ios::binary);
        f << "line1\r\nline2\r\nline3\r\n";
    }

    FileService fs;
    auto result = fs.read(L"test_data/temp_test_crlf.txt");

    REQUIRE(result.has_value());
    // Should contain \n but not \r
    CHECK(result->raw_utf8.find('\r') == std::string::npos);
    CHECK(result->raw_utf8.find('\n') != std::string::npos);
    CHECK(result->raw_utf8 == "line1\nline2\nline3\n");

    std::remove(path);
}

TEST_CASE("normalize lone CR line endings") {
    const char* path = "test_data/temp_test_cr.txt";

    {
        std::ofstream f(path, std::ios::binary);
        f << "alpha\rbeta\rgamma\r";
    }

    FileService fs;
    auto result = fs.read(L"test_data/temp_test_cr.txt");

    REQUIRE(result.has_value());
    CHECK(result->raw_utf8.find('\r') == std::string::npos);
    CHECK(result->raw_utf8 == "alpha\nbeta\ngamma\n");

    std::remove(path);
}

TEST_CASE("identity returns correct size for known file") {
    FileService fs;
    auto ident = fs.identity(L"test_data/sample.md");

    REQUIRE(ident.has_value());
    CHECK(ident->path == L"test_data/sample.md");
    CHECK(ident->size > 0);
    CHECK(ident->mtime > 0);

    // Cross-check: identity size should match read content size
    auto content = fs.read(L"test_data/sample.md");
    REQUIRE(content.has_value());
    CHECK(ident->size == content->identity.size);
    CHECK(ident->mtime == content->identity.mtime);
}

TEST_CASE("identity returns nullopt for nonexistent file") {
    FileService fs;
    auto ident = fs.identity(L"test_data/no_such_file.md");

    CHECK_FALSE(ident.has_value());
}

TEST_CASE("UTF-8 BOM is stripped from content") {
    const char* path = "test_data/temp_test_bom.txt";

    {
        std::ofstream f(path, std::ios::binary);
        // Write UTF-8 BOM followed by content
        f << '\xEF' << '\xBB' << '\xBF' << "Hello BOM";
    }

    FileService fs;
    auto result = fs.read(L"test_data/temp_test_bom.txt");

    REQUIRE(result.has_value());
    CHECK(result->raw_utf8 == "Hello BOM");
    CHECK(result->raw_utf8.find('\xEF') == std::string::npos);
    CHECK(result->text == L"Hello BOM");

    std::remove(path);
}

TEST_CASE("UTF-16 LE BOM file is decoded correctly") {
    const char* path = "test_data/temp_test_utf16le.txt";

    {
        std::ofstream f(path, std::ios::binary);
        // UTF-16 LE BOM
        f << '\xFF' << '\xFE';
        // "Hi" in UTF-16 LE: H=0x48,0x00  i=0x69,0x00
        f << '\x48' << '\x00' << '\x69' << '\x00';
    }

    FileService fs;
    auto result = fs.read(L"test_data/temp_test_utf16le.txt");

    REQUIRE(result.has_value());
    CHECK(result->raw_utf8 == "Hi");
    CHECK(result->text == L"Hi");

    std::remove(path);
}

TEST_CASE("UTF-16 BE BOM file is decoded correctly") {
    const char* path = "test_data/temp_test_utf16be.txt";

    {
        std::ofstream f(path, std::ios::binary);
        // UTF-16 BE BOM
        f << '\xFE' << '\xFF';
        // "Hi" in UTF-16 BE: H=0x00,0x48  i=0x00,0x69
        f << '\x00' << '\x48' << '\x00' << '\x69';
    }

    FileService fs;
    auto result = fs.read(L"test_data/temp_test_utf16be.txt");

    REQUIRE(result.has_value());
    CHECK(result->raw_utf8 == "Hi");
    CHECK(result->text == L"Hi");

    std::remove(path);
}

TEST_CASE("read empty file returns empty content") {
    const char* path = "test_data/temp_test_empty.txt";

    {
        std::ofstream f(path, std::ios::binary);
        // Write nothing — empty file
    }

    FileService fs;
    auto result = fs.read(L"test_data/temp_test_empty.txt");

    REQUIRE(result.has_value());
    CHECK(result->raw_utf8.empty());
    CHECK(result->text.empty());
    CHECK(result->identity.size == 0);

    std::remove(path);
}
