#pragma once

#include "tools/screenshot/options.h"

#include <string>

namespace wlx::tools::screenshot {

// Run the markdown rendering pipeline (read -> parse -> layout -> paint -> save).
// Returns the path of the written PNG on success, empty string on failure
// (error already printed to stderr). Caller is responsible for
// CoInitialize/CoUninitialize -- all COM objects are created and destroyed
// inside this function.
std::wstring run_markdown_pipeline(const Options& opts);

}  // namespace wlx::tools::screenshot
