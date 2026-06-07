#pragma once

#include "wlx_core/abi.h"
#include "core_dll/grammar/grammar_cache.h"
#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include <tree_sitter/api.h>

namespace wlx::core::grammar {


class WLX_CORE_API GrammarRegistry {
public:
    GrammarRegistry(const std::wstring& grammar_dir,
                    uint32_t cap = 8,
                    std::chrono::seconds ttl = std::chrono::seconds(5 * 60),
                    GrammarCache::Clock clock = std::chrono::steady_clock::now,
                    GrammarCache::Loader loader = {},
                    GrammarCache::Releaser releaser = {});

    GrammarRegistry(const GrammarRegistry&) = delete;
    GrammarRegistry& operator=(const GrammarRegistry&) = delete;

    bool supports(const std::string& language) const;
    std::vector<std::string> available_languages() const;

    const TSLanguage* get_grammar(const std::string& language);
    const TSQuery*    get_query(const std::string& language);
    TSTree*           parse(const std::string& language, std::string_view source);

private:
    void scan_directory(const std::wstring& grammar_dir);

    GrammarCache cache_;
};

}  // namespace wlx::core::grammar
