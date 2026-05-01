#pragma once

#include "wlx_core_api.h"

#include <chrono>
#include <functional>
#include <list>
#include <string>
#include <tree_sitter/api.h>
#include <unordered_map>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class WLX_CORE_API GrammarCache {
public:
    using SteadyTp = std::chrono::steady_clock::time_point;
    using Clock    = std::function<SteadyTp()>;

    // Loader returns a pair (HMODULE, TSLanguage*) on success, both null on
    // failure. Injection point for tests.
    struct LoadResult {
        HMODULE handle = nullptr;
        const TSLanguage* language = nullptr;
    };
    using Loader = std::function<LoadResult(const std::wstring& dll_path,
                                            const std::string& language)>;

    // Releases an HMODULE returned by Loader. Default is FreeLibrary; tests
    // override with a no-op so synthetic handles don't get passed to the
    // Windows loader.
    using Releaser = std::function<void(HMODULE handle)>;

    GrammarCache(uint32_t cap,
                 std::chrono::seconds ttl,
                 Clock clock = std::chrono::steady_clock::now,
                 Loader loader = default_loader(),
                 Releaser releaser = default_releaser());

    ~GrammarCache();

    GrammarCache(const GrammarCache&) = delete;
    GrammarCache& operator=(const GrammarCache&) = delete;

    // Register an entry's metadata (path + raw query source). Called once
    // per grammar at scan time. Idempotent.
    void register_entry(const std::string& language,
                        std::wstring dll_path,
                        std::string query_source);

    // Returns the loaded TSLanguage, loading the DLL on first call. Updates
    // LRU/last_used. Triggers eviction sweep if (cap exceeded AND tail
    // stale). Returns nullptr if entry unknown or load failed previously.
    //
    // Failures are sticky: once load_attempted is set with no handle (e.g.
    // LoadLibraryW returned null, or the export wasn't found), subsequent
    // calls return nullptr without retrying. An evict-and-reload sequence
    // is the only way to retry.
    const TSLanguage* get_grammar(const std::string& language);

    // Returns compiled query, lazily compiling from the registered
    // query_source. Returns nullptr if no source or compile failed.
    const TSQuery* get_query(const std::string& language);

    // Test-only inspection.
    bool   is_loaded(const std::string& language) const;
    size_t loaded_count() const { return loaded_count_; }
    bool   is_known(const std::string& language) const {
        return entries_.find(language) != entries_.end();
    }
    std::vector<std::string> available_languages() const;

    // Read-only access to the raw, unresolved query source for a language.
    // Used by get_query to walk `; inherits:` chains.
    std::string raw_query_source(const std::string& language) const;

private:
    static Loader default_loader();
    static Releaser default_releaser();
    void evict_locked();

    struct Entry {
        std::wstring dll_path;
        std::string  query_source;
        HMODULE      handle = nullptr;
        const TSLanguage* language = nullptr;
        TSQuery*     query = nullptr;
        bool         load_attempted = false;
        bool         query_compiled = false;
        SteadyTp     last_used{};
        std::list<std::string>::iterator lru_pos{};
        // Invariant: lru_pos is valid iff handle != nullptr. After eviction
        // the iterator is dangling but handle is also nulled, so the next
        // get_grammar reseats both atomically.
    };

    std::unordered_map<std::string, Entry> entries_;
    std::list<std::string> lru_;       // MRU at front
    size_t   loaded_count_ = 0;
    uint32_t cap_;
    std::chrono::seconds ttl_;
    Clock    clock_;
    Loader   loader_;
    Releaser releaser_;
};
