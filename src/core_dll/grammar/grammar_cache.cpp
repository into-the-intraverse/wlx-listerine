#define NOMINMAX
#include "core_dll/grammar/grammar_cache.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace wlx::core::grammar {


GrammarCache::GrammarCache(uint32_t cap,
                           std::chrono::seconds ttl,
                           Clock clock,
                           Loader loader,
                           Releaser releaser)
    : cap_(cap)
    , ttl_(ttl)
    , clock_(clock ? std::move(clock) : std::chrono::steady_clock::now)
    , loader_(loader ? std::move(loader) : default_loader())
    , releaser_(releaser ? std::move(releaser) : default_releaser())
{}

GrammarCache::~GrammarCache() {
    // Match the existing GrammarRegistry destructor behavior. In production
    // the cache is owned by the leaked CoreRegistry and never destructs.
    for (auto& [name, e] : entries_) {
        if (e.query) ts_query_delete(e.query);
        if (e.handle) releaser_(e.handle);
    }
}

GrammarCache::Releaser GrammarCache::default_releaser() {
    return [](HMODULE h) { FreeLibrary(h); };
}

GrammarCache::Loader GrammarCache::default_loader() {
    return [](const std::wstring& dll_path,
              const std::string& language) -> LoadResult {
        LoadResult r;
        r.handle = LoadLibraryW(dll_path.c_str());
        if (!r.handle) return r;
        std::string fn_name = "tree_sitter_" + language;
        std::replace(fn_name.begin(), fn_name.end(), '-', '_');
        using GrammarFn = const TSLanguage* (*)();
        auto fn = reinterpret_cast<GrammarFn>(
            GetProcAddress(r.handle, fn_name.c_str()));
        if (!fn) {
            FreeLibrary(r.handle);
            r.handle = nullptr;
            return r;
        }
        r.language = fn();
        return r;
    };
}

void GrammarCache::register_entry(const std::string& language,
                                  std::wstring dll_path,
                                  std::string query_source) {
    // Normalize CRLF -> LF so the `; inherits:` parser in resolve_query_source
    // (which splits on '\n' alone) doesn't drag a stray '\r' into the parent
    // grammar lookup. .scm files arrive with either EOL convention depending
    // on how Git checks them out: core.autocrlf=true on Windows yields CRLF,
    // which previously broke `; inherits: c\r\n` -> `raw_query_source("c\r")`
    // -> empty -> cpp lost all C-derived queries.
    query_source.erase(std::remove(query_source.begin(), query_source.end(), '\r'),
                       query_source.end());
    auto& e = entries_[language];
    e.dll_path = std::move(dll_path);
    e.query_source = std::move(query_source);
    e.scm_loaded = true;  // source provided directly; no deferred file read
}

void GrammarCache::register_entry_path(const std::string& language,
                                       std::wstring dll_path,
                                       std::wstring scm_path) {
    auto& e = entries_[language];
    e.dll_path = std::move(dll_path);
    e.scm_path = std::move(scm_path);
    e.scm_loaded = false;  // highlights.scm read lazily on first query use
}

const std::string& GrammarCache::source_for(const Entry& e) const {
    if (!e.scm_loaded) {
        if (!e.scm_path.empty()) {
            std::ifstream ifs(e.scm_path, std::ios::binary);
            if (ifs) {
                std::ostringstream oss;
                oss << ifs.rdbuf();
                e.query_source = oss.str();
            }
        }
        // Normalize CRLF -> LF (see register_entry's note: the `; inherits:`
        // parser splits on '\n' alone, so a stray '\r' would corrupt parent
        // grammar lookup). Applied here so the deferred read matches the
        // eager register_entry path.
        e.query_source.erase(
            std::remove(e.query_source.begin(), e.query_source.end(), '\r'),
            e.query_source.end());
        e.scm_loaded = true;
    }
    return e.query_source;
}

const TSLanguage* GrammarCache::get_grammar(const std::string& language) {
    auto it = entries_.find(language);
    if (it == entries_.end()) return nullptr;
    auto& e = it->second;

    if (!e.handle && !e.load_attempted) {
        e.load_attempted = true;
        auto r = loader_(e.dll_path, language);
        if (!r.handle || !r.language) return nullptr;
        e.handle = r.handle;
        e.language = r.language;
        loaded_count_++;
        lru_.push_front(language);
        e.lru_pos = lru_.begin();
        e.last_used = clock_();
        if (loaded_count_ > cap_) evict_locked();
        return e.language;
    }
    if (e.handle) {
        // Promote to MRU.
        lru_.splice(lru_.begin(), lru_, e.lru_pos);
        e.last_used = clock_();
        return e.language;
    }
    // load_attempted && !handle -> permanent failure.
    return nullptr;
}

const TSQuery* GrammarCache::get_query(const std::string& language) {
    auto it = entries_.find(language);
    if (it == entries_.end()) return nullptr;
    auto& e = it->second;
    if (e.query) return e.query;
    if (e.query_compiled) return nullptr;
    e.query_compiled = true;

    const TSLanguage* lang = get_grammar(language);
    if (!lang) return nullptr;

    // Resolve the `; inherits:` chain (recursive — see resolved_query_source).
    std::string source = resolved_query_source(language);
    if (source.empty()) return nullptr;

    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    e.query = ts_query_new(lang, source.c_str(),
                           static_cast<uint32_t>(source.size()),
                           &error_offset, &error_type);
    return e.query;
}

std::string GrammarCache::raw_query_source(const std::string& language) const {
    auto it = entries_.find(language);
    return (it == entries_.end()) ? std::string{} : source_for(it->second);
}

std::string GrammarCache::resolved_query_source(const std::string& language) const {
    std::vector<std::string> visited;
    return resolve_query_source(language, visited, 0);
}

std::string GrammarCache::resolve_query_source(const std::string& language,
                                               std::vector<std::string>& visited,
                                               int depth) const {
    // A `; inherits:` first line names parent grammars whose query sources
    // are spliced in front of this language's own rules. Each parent is
    // resolved RECURSIVELY so multi-level chains (unreal-cpp -> cpp -> c)
    // flatten fully — splicing raw parent sources used to demote a parent's
    // own `; inherits:` line to an inert comment and silently drop the
    // grandparents' rules.
    static constexpr int kMaxInheritDepth = 8;
    if (depth > kMaxInheritDepth) return {};
    if (std::find(visited.begin(), visited.end(), language) != visited.end())
        return {};  // inheritance cycle
    visited.push_back(language);

    std::string source = raw_query_source(language);
    if (source.empty()) return source;

    auto first_newline = source.find('\n');
    std::string first_line = (first_newline != std::string::npos)
                              ? source.substr(0, first_newline)
                              : source;
    const std::string prefix = "; inherits:";
    auto pos = first_line.find(prefix);
    if (pos == std::string::npos) return source;

    std::string parents_str = first_line.substr(pos + prefix.size());
    std::string rest = (first_newline != std::string::npos)
                        ? source.substr(first_newline + 1)
                        : std::string{};
    std::string combined;
    std::istringstream iss(parents_str);
    std::string parent;
    while (std::getline(iss, parent, ',')) {
        size_t s = parent.find_first_not_of(" \t(");
        size_t en = parent.find_last_not_of(" \t)");
        if (s == std::string::npos || en == std::string::npos || s > en)
            continue;
        std::string ps = resolve_query_source(parent.substr(s, en - s + 1),
                                              visited, depth + 1);
        if (!ps.empty()) {
            combined += ps;
            combined += '\n';
        }
    }
    combined += rest;
    return combined;
}

void GrammarCache::pin(const std::string& language) {
    auto it = entries_.find(language);   // not operator[]: never create an entry
    if (it != entries_.end()) it->second.pin_count++;
}

void GrammarCache::unpin(const std::string& language) {
    auto it = entries_.find(language);
    if (it != entries_.end() && it->second.pin_count > 0)
        it->second.pin_count--;
}

bool GrammarCache::is_loaded(const std::string& language) const {
    auto it = entries_.find(language);
    return it != entries_.end() && it->second.handle != nullptr;
}

std::vector<std::string> GrammarCache::available_languages() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (auto& [name, _] : entries_) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

void GrammarCache::evict_locked() {
    auto now = clock_();
    // Walk oldest (back) -> newest (front). Skip pinned entries (a live tree
    // references their TSLanguage, so freeing the DLL would dangle it). For
    // unpinned entries: if fresh (age < ttl_) stop, since everything newer is
    // fresher; otherwise evict. `lru_pos` stored in OTHER entries stays valid
    // across a middle erase (std::list::erase only invalidates the erased node);
    // the evicted entry's now-dangling lru_pos is nulled together with its
    // handle, preserving the "lru_pos valid iff handle != nullptr" invariant.
    auto it = lru_.end();
    while (loaded_count_ > cap_ && it != lru_.begin()) {
        --it;                                   // now points at a real element
        auto eit = entries_.find(*it);
        if (eit == entries_.end()) {            // stale lru_ node, no entry
            it = lru_.erase(it);
            continue;
        }
        Entry& e = eit->second;
        if (e.pin_count > 0) continue;          // pinned: never evict, keep scanning
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - e.last_used);
        // Fresh unpinned: the LRU is monotone in last_used, so every still-
        // unvisited (newer) entry is fresh too -> nothing left to evict. (A
        // skipped pinned entry can't change that; continue/break are equivalent.)
        if (age < ttl_) continue;

        if (e.query) { ts_query_delete(e.query); e.query = nullptr; }
        if (e.handle) { releaser_(e.handle); e.handle = nullptr; }
        e.language = nullptr;
        e.load_attempted = false;
        e.query_compiled = false;
        loaded_count_--;
        it = lru_.erase(it);                    // returns the element AFTER `it`
    }
}

}  // namespace wlx::core::grammar
