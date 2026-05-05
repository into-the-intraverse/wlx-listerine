#pragma once

#include "runtime/parser/block_node.h"

#include <vector>

namespace wlx::runtime::parser {


struct Document {
    std::vector<BlockNode> blocks;
};

}  // namespace wlx::runtime::parser
