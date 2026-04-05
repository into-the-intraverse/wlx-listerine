#include "config.h"

Config default_config() { return {}; }
Config load_config(const std::string& path) { return default_config(); }
uint32_t parse_hex_color(const std::string& hex) { return 0; }
