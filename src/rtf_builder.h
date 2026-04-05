#pragma once

#include "config.h"
#include <string>
#include <vector>

struct LinkInfo {
    size_t char_start;
    size_t char_end;
    std::string url;
};

class RtfBuilder {
public:
    RtfBuilder(const Config& config, bool dark_mode);
    std::string build(const char* markdown, size_t length);
    const std::vector<LinkInfo>& links() const { return links_; }

private:
    const Config& config_;
    const ColorScheme& colors_;
    std::string rtf_;
    std::vector<LinkInfo> links_;
};
