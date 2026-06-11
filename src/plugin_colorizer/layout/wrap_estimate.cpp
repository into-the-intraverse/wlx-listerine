#include "plugin_colorizer/layout/wrap_estimate.h"

namespace wlx::plugin_colorizer::layout {

std::vector<int> estimate_wrap_rows(const std::string& raw_utf8,
                                    const std::vector<int>& line_byte_starts,
                                    int tab_width, int cols_per_row) {
    std::vector<int> rows;
    rows.reserve(line_byte_starts.size());
    const int size = static_cast<int>(raw_utf8.size());
    const int tw = tab_width > 0 ? tab_width : 1;
    for (size_t li = 0; li < line_byte_starts.size(); ++li) {
        const int begin = line_byte_starts[li];
        const int end = (li + 1 < line_byte_starts.size())
                            ? line_byte_starts[li + 1]
                            : size;
        int cols = 0;
        for (int i = begin; i < end; ++i) {
            const unsigned char b = static_cast<unsigned char>(raw_utf8[static_cast<size_t>(i)]);
            // A lone mid-line \r also stops counting — intentional
            // underestimate, corrected at materialization.
            if (b == '\n' || b == '\r') break;
            if (b == '\t') {
                cols = (cols / tw + 1) * tw;
                continue;
            }
            if ((b & 0xC0) == 0x80) continue;  // UTF-8 continuation byte
            ++cols;
        }
        const int r = (cols_per_row <= 0 || cols == 0)
                          ? 1
                          : (cols + cols_per_row - 1) / cols_per_row;
        rows.push_back(r);
    }
    return rows;
}

std::vector<int> build_row_starts(const std::vector<int>& rows) {
    std::vector<int> starts;
    starts.reserve(rows.size() + 1);
    starts.push_back(0);
    for (int r : rows) starts.push_back(starts.back() + r);
    return starts;
}

}  // namespace wlx::plugin_colorizer::layout
