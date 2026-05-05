#pragma once

#include "runtime/parser/block_node.h"

#include <vector>

struct Document {
    std::vector<BlockNode> blocks;
};
