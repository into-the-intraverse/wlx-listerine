#include "rtf_builder.h"

RtfBuilder::RtfBuilder(const Config& config, bool dark_mode)
    : config_(config)
    , colors_(dark_mode ? config.dark : config.light) {}

std::string RtfBuilder::build(const char* markdown, size_t length) {
    return "{\\rtf1\\ansi\\deff0 stub}";
}
