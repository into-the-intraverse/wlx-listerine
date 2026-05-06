#include "tools/screenshot/colorizer_pipeline.h"

#include <cstdio>

namespace wlx::tools::screenshot {

std::wstring run_colorizer_pipeline(const Options& /*opts*/) {
    std::fprintf(stderr,
        "Colorizer pipeline not implemented yet "
        "(lands in Stages 3.3 paint / 3.4 dump-tokens)\n");
    return {};
}

}  // namespace wlx::tools::screenshot
