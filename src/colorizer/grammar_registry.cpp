#define NOMINMAX
#include "grammar_registry.h"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

using GrammarFn = const TSLanguage* (*)();

GrammarRegistry::GrammarRegistry(const std::wstring& grammar_dir)
    : grammar_dir_(grammar_dir) {
    scan_directory();
}

GrammarRegistry::~GrammarRegistry() {
    for (auto& [name, entry] : grammars_) {
        if (entry.query)
            ts_query_delete(entry.query);
        if (entry.handle)
            FreeLibrary(entry.handle);
    }
}

void GrammarRegistry::scan_directory() {
    std::error_code ec;
    if (!fs::is_directory(grammar_dir_, ec))
        return;

    for (auto& subdir : fs::directory_iterator(grammar_dir_, ec)) {
        if (!subdir.is_directory()) continue;

        std::string lang = subdir.path().filename().string();
        std::wstring dll_path;
        std::string query_source;

        // Look for tree-sitter-<lang>.dll and highlights.scm in subdirectory
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
            GrammarEntry ge;
            ge.dll_path = dll_path;
            ge.query_source = std::move(query_source);
            grammars_[lang] = std::move(ge);
        }
    }
}

bool GrammarRegistry::supports(const std::string& language) const {
    return grammars_.find(language) != grammars_.end();
}

std::vector<std::string> GrammarRegistry::available_languages() const {
    std::vector<std::string> result;
    result.reserve(grammars_.size());
    for (auto& [name, entry] : grammars_)
        result.push_back(name);
    std::sort(result.begin(), result.end());
    return result;
}

const TSLanguage* GrammarRegistry::get_grammar(const std::string& language) {
    auto it = grammars_.find(language);
    if (it == grammars_.end()) return nullptr;

    auto& entry = it->second;
    if (entry.language) return entry.language;
    if (entry.load_attempted) return nullptr;

    entry.load_attempted = true;
    entry.handle = LoadLibraryW(entry.dll_path.c_str());
    if (!entry.handle) return nullptr;

    std::string fn_name = "tree_sitter_" + language;
    std::replace(fn_name.begin(), fn_name.end(), '-', '_');

    auto fn = reinterpret_cast<GrammarFn>(GetProcAddress(entry.handle, fn_name.c_str()));
    if (!fn) {
        FreeLibrary(entry.handle);
        entry.handle = nullptr;
        return nullptr;
    }

    entry.language = fn();
    return entry.language;
}

const TSQuery* GrammarRegistry::get_query(const std::string& language) {
    auto it = grammars_.find(language);
    if (it == grammars_.end()) return nullptr;

    auto& entry = it->second;
    if (entry.query) return entry.query;
    if (entry.query_compiled) return nullptr;

    entry.query_compiled = true;

    const TSLanguage* lang = get_grammar(language);
    if (!lang) return nullptr;

    std::string source = resolve_query_source(language);
    if (source.empty()) return nullptr;

    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    entry.query = ts_query_new(lang, source.c_str(),
                               static_cast<uint32_t>(source.size()),
                               &error_offset, &error_type);
    return entry.query;
}

TSTree* GrammarRegistry::parse(const std::string& language, const std::string& source) {
    const TSLanguage* lang = get_grammar(language);
    if (!lang) return nullptr;

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, lang);
    TSTree* tree = ts_parser_parse_string(parser, nullptr,
                                          source.c_str(),
                                          static_cast<uint32_t>(source.size()));
    ts_parser_delete(parser);
    return tree;
}

std::string GrammarRegistry::resolve_query_source(const std::string& language, int depth) {
    if (depth > 5) return {};

    auto it = grammars_.find(language);
    if (it == grammars_.end()) return {};

    const std::string& raw = it->second.query_source;
    if (raw.empty()) return {};

    // Check first line for "; inherits: lang1, lang2"
    auto first_newline = raw.find('\n');
    std::string first_line = (first_newline != std::string::npos)
                             ? raw.substr(0, first_newline)
                             : raw;

    const std::string prefix = "; inherits:";
    auto pos = first_line.find(prefix);
    if (pos == std::string::npos) return raw;

    // Parse parent language names after "; inherits:"
    std::string parents_str = first_line.substr(pos + prefix.size());
    std::string rest = (first_newline != std::string::npos)
                       ? raw.substr(first_newline + 1)
                       : std::string{};

    std::string result;
    std::istringstream iss(parents_str);
    std::string parent;
    while (std::getline(iss, parent, ',')) {
        // Trim whitespace and optional parens
        size_t start = parent.find_first_not_of(" \t(");
        size_t end = parent.find_last_not_of(" \t)");
        if (start == std::string::npos || end == std::string::npos || start > end)
            continue;
        std::string parent_name = parent.substr(start, end - start + 1);

        std::string parent_source = resolve_query_source(parent_name, depth + 1);
        if (!parent_source.empty()) {
            result += parent_source;
            result += '\n';
        }
    }

    result += rest;
    return result;
}
