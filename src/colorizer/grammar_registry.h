#pragma once

#include "wlx_core_api.h"
#include "grammar_cache.h"
#include <chrono>
#include <string>
#include <vector>
#include <tree_sitter/api.h>

class WLX_CORE_API GrammarRegistry {
public:
    GrammarRegistry(const std::wstring& grammar_dir,
                    uint32_t cap = 8,
                    std::chrono::seconds ttl = std::chrono::seconds(5 * 60),
                    GrammarCache::Clock clock = std::chrono::steady_clock::now,
                    GrammarCache::Loader loader = {});

    GrammarRegistry(const GrammarRegistry&) = delete;
    GrammarRegistry& operator=(const GrammarRegistry&) = delete;

    bool supports(const std::string& language) const;
    std::vector<std::string> available_languages() const;

    const TSLanguage* get_grammar(const std::string& language);
    const TSQuery*    get_query(const std::string& language);
    TSTree*           parse(const std::string& language, const std::string& source);

private:
    void scan_directory(const std::wstring& grammar_dir);

    GrammarCache cache_;
};
