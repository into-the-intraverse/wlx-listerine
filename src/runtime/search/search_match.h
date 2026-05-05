#pragma once

struct SearchMatch {
    int block_index = -1;
    int char_start = 0;  // offset into block's flattened text (UTF-16 code units)
    int char_end = 0;
};
