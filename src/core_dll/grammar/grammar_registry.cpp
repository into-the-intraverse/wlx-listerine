#define NOMINMAX
#include "core_dll/grammar/grammar_registry.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

GrammarRegistry::GrammarRegistry(const std::wstring& grammar_dir,
                                 uint32_t cap,
                                 std::chrono::seconds ttl,
                                 GrammarCache::Clock clock,
                                 GrammarCache::Loader loader,
                                 GrammarCache::Releaser releaser)
    : cache_(cap, ttl, std::move(clock), std::move(loader), std::move(releaser))
{
    scan_directory(grammar_dir);
}

void GrammarRegistry::scan_directory(const std::wstring& grammar_dir) {
    std::error_code ec;
    if (!fs::is_directory(grammar_dir, ec)) return;

    for (auto& subdir : fs::directory_iterator(grammar_dir, ec)) {
        if (!subdir.is_directory()) continue;
        std::string lang = subdir.path().filename().string();
        std::wstring dll_path;
        std::string query_source;
        for (auto& file : fs::directory_iterator(subdir.path(), ec)) {
            if (!file.is_regular_file()) continue;
            auto filename = file.path().filename().string();
            if (filename.size() >= 16 &&
                filename.substr(0, 12) == "tree-sitter-" &&
                filename.substr(filename.size() - 4) == ".dll") {
                dll_path = file.path().wstring();
            } else if (filename == "highlights.scm") {
                std::ifstream ifs(file.path(), std::ios::binary);
                if (ifs) {
                    std::ostringstream oss;
                    oss << ifs.rdbuf();
                    query_source = oss.str();
                }
            }
        }
        if (!dll_path.empty()) {
            cache_.register_entry(lang, std::move(dll_path), std::move(query_source));
        }
    }
}

bool GrammarRegistry::supports(const std::string& language) const {
    return cache_.is_known(language);
}

std::vector<std::string> GrammarRegistry::available_languages() const {
    return cache_.available_languages();
}

const TSLanguage* GrammarRegistry::get_grammar(const std::string& language) {
    return cache_.get_grammar(language);
}

const TSQuery* GrammarRegistry::get_query(const std::string& language) {
    return cache_.get_query(language);
}

TSTree* GrammarRegistry::parse(const std::string& language,
                               const std::string& source) {
    const TSLanguage* lang = cache_.get_grammar(language);
    if (!lang) return nullptr;
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, lang);
    TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                          source.c_str(),
                                          static_cast<uint32_t>(source.size()));
    ts_parser_delete(parser);
    return tree;
}
