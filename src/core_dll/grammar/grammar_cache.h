#pragma once

#include "wlx_core/abi.h"

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

namespace wlx::core::grammar {


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

    // Like register_entry but defers reading highlights.scm: stores the path
    // and slurps it lazily on first query use (avoids reading every grammar's
    // .scm at startup when only one language is opened). Used by scan_directory.
    void register_entry_path(const std::string& language,
                             std::wstring dll_path,
                             std::wstring scm_path);

    // Returns the loaded TSLanguage, loading the DLL on first call. Updates
    // LRU/last_used. Triggers eviction sweep if (cap exceeded AND tail
    // stale). Returns nullptr if entry unknown or load failed previously.
    //
    // Failures are sticky: once load_attempted is set with no handle (e.g.
    // LoadLibraryW returned null, or the export wasn't found), subsequent
    // calls return nullptr without retrying. Failed entries never enter the
    // LRU, so eviction can't reset them — the failure persists until process
    // restart.
    const TSLanguage* get_grammar(const std::string& language);

    // Returns compiled query, lazily compiling from the registered
    // query_source. Returns nullptr if no source or compile failed.
    const TSQuery* get_query(const std::string& language);

    // Pin a loaded grammar so eviction never frees it while a cached tree
    // references its TSLanguage. Balanced by unpin(). No-op for unknown langs.
    void pin(const std::string& language);
    // Release one pin. No-op for unknown langs / pin_count already 0.
    void unpin(const std::string& language);

    // Test-only inspection.
    bool   is_loaded(const std::string& language) const;
    size_t loaded_count() const { return loaded_count_; }
    uint32_t pin_count_of(const std::string& language) const {
        auto it = entries_.find(language);
        return it == entries_.end() ? 0u : it->second.pin_count;
    }
    bool   is_known(const std::string& language) const {
        return entries_.find(language) != entries_.end();
    }
    std::vector<std::string> available_languages() const;

    // Read-only access to the raw, unresolved query source for a language.
    std::string raw_query_source(const std::string& language) const;

    // Like raw_query_source, but with the `; inherits:` chain resolved
    // recursively (each parent's own chain flattens too, so unreal-cpp ->
    // cpp -> c picks up c's rules). Cycle-guarded and depth-capped. Used by
    // get_query before compiling.
    std::string resolved_query_source(const std::string& language) const;

private:
    static Loader default_loader();
    static Releaser default_releaser();
    void evict_locked();

    // Recursive worker for resolved_query_source. `visited` guards
    // inheritance cycles; `depth` caps pathological chains.
    std::string resolve_query_source(const std::string& language,
                                     std::vector<std::string>& visited,
                                     int depth) const;

    struct Entry {
        std::wstring dll_path;
        std::wstring scm_path;             // deferred highlights.scm source path
        // Lazily slurped from scm_path (CRLF-stripped) on first use, or set
        // directly by register_entry. mutable so raw_query_source stays const.
        mutable std::string query_source;
        mutable bool scm_loaded = false;   // query_source is populated/finalized
        HMODULE      handle = nullptr;
        const TSLanguage* language = nullptr;
        TSQuery*     query = nullptr;
        bool         load_attempted = false;
        bool         query_compiled = false;
        uint32_t     pin_count = 0;        // live cached trees referencing this
                                           // grammar's TSLanguage; >0 blocks eviction
        SteadyTp     last_used{};
        std::list<std::string>::iterator lru_pos{};
        // Invariant: lru_pos is valid iff handle != nullptr. After eviction
        // the iterator is dangling but handle is also nulled, so the next
        // get_grammar reseats both atomically.
    };

    // Returns the entry's query source, reading scm_path on first call.
    const std::string& source_for(const Entry& e) const;

    std::unordered_map<std::string, Entry> entries_;
    std::list<std::string> lru_;       // MRU at front
    size_t   loaded_count_ = 0;
    uint32_t cap_;
    std::chrono::seconds ttl_;
    Clock    clock_;
    Loader   loader_;
    Releaser releaser_;
};

}  // namespace wlx::core::grammar
