#pragma once

#include "tools/screenshot/options.h"

#include <string>

namespace wlx::tools::screenshot {

// Run the colorizer pipeline. In paint mode, writes a PNG and returns its path.
// In --dump-tokens mode, writes a JSON file and returns its path.
// Returns empty string on failure (error already printed to stderr).
//
// Implementation is staged: 3.3 (committed) lands the paint path; 3.4 wires
// --dump-tokens (calls TokenJsonWriter); 3.5 wires --display-config TOML.
std::wstring run_colorizer_pipeline(const Options& opts);

}  // namespace wlx::tools::screenshot
