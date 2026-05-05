#pragma once

#include <cstdint>

namespace wlx::runtime::parser {


struct SourceRange {
    uint32_t offset = 0;
    uint32_t length = 0;
};

}  // namespace wlx::runtime::parser
