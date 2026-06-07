#pragma once

#include "runtime/io/file_content.h"
#include "runtime/io/file_identity.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wlx::runtime::io {


class FileService {
public:
    std::optional<FileContent> read(const wchar_t* path);
    std::optional<FileIdentity> identity(const wchar_t* path);

private:
    std::string read_bytes(const wchar_t* path, FileIdentity& out_identity);
    std::string to_utf8(std::string raw);
    std::wstring to_wstring(const std::string& utf8);
    void normalize_line_endings(std::string& text);
};

}  // namespace wlx::runtime::io
