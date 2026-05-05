#pragma once

#include "runtime/io/file_identity.h"

#include <string>

namespace wlx::runtime::io {


struct FileContent {
    std::string raw_utf8;        // Original UTF-8 bytes for md4c parser
    std::wstring text;           // Decoded + normalized for DirectWrite
    FileIdentity identity;
};

}  // namespace wlx::runtime::io
