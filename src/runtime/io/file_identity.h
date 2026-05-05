#pragma once

#include <cstdint>
#include <string>

struct FileIdentity {
    std::wstring path;
    uint64_t size = 0;
    uint64_t mtime = 0;
};
