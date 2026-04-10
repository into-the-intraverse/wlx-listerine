#pragma once

#include <tree_sitter/api.h>
#include <string>
#include <vector>

struct TokenSpan {
    uint32_t start = 0;   // byte offset
    uint32_t length = 0;
    std::string node_type;
};

class Tokenizer {
public:
    static std::vector<TokenSpan> tokenize(const TSLanguage* grammar,
                                            const std::string& source);
};
