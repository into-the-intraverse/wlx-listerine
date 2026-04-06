#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

struct FileIdentity {
    std::wstring path;
    uint64_t size = 0;
    uint64_t mtime = 0;
};

struct FileContent {
    std::string raw_utf8;        // Original UTF-8 bytes for md4c parser
    std::wstring text;           // Decoded + normalized for DirectWrite
    FileIdentity identity;
};

class FileService {
public:
    std::optional<FileContent> read(const wchar_t* path);
    std::optional<FileIdentity> identity(const wchar_t* path);

private:
    std::vector<uint8_t> read_bytes(const wchar_t* path, FileIdentity& out_identity);
    std::string to_utf8(const std::vector<uint8_t>& raw);
    std::wstring to_wstring(const std::string& utf8);
    void normalize_line_endings(std::string& text);
};
