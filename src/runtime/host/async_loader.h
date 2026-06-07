#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace wlx::runtime::host {

// Liveness token: heap object shared between a view and its in-flight worker(s).
// Outlives the ViewState deliberately (shared_ptr) so a late worker can safely
// read `closed`/`generation` after the ViewState is gone. COM-free; carries NO HWND.
struct ViewLiveToken {
    std::atomic<uint64_t> generation{0};  // == the latest load's generation for this view
    std::atomic<bool>     closed{false};  // set true in ListCloseWindow BEFORE deleting the
                                          // ViewState; the worker re-checks it after parse to
                                          // skip PostMessage to a window that's going away.
};

// Per-plugin-module (statically linked): a strictly-monotonic load counter and a
// detach flag. Each .wlx64 gets its own instances — that's correct here (see brief).
inline std::atomic<uint64_t> g_load_gen{0};
inline std::atomic<bool>     g_shutting_down{false};

// Per-view load state, stored on each plugin's ViewState (not consumed by this
// unit): Loading until the async parse result is adopted, then Ready.
enum class LoadState : uint8_t { Loading, Ready };

// Generic, COM-free job carried to the worker. Plugin-specific parse inputs are
// captured in the parse closure, NOT added here.
//
// Result ownership: spawn_parse_worker PostMessages a raw Result* (ownership moves
// to the UI thread). The WndProc handler for done_msg MUST wrap lParam in a
// unique_ptr<Result> immediately, then call should_adopt_result to decide whether
// to keep it — every reject path then frees it on scope exit (no leak).
struct ParseJob {
    std::wstring path;                         // deep copy (host buffer is transient)
    uint64_t     generation = 0;               // g_load_gen value stamped for this load
    std::shared_ptr<ViewLiveToken> live;       // same control block as the spawning view
    HWND         hwnd = nullptr;               // PostMessage target ONLY
    UINT         done_msg = 0;                 // this plugin's RegisterWindowMessageW value
};

// Pure UI-thread adoption guard (UNIT-TESTABLE). Returns true iff a result with
// (result_gen, result_token) should be adopted by the view (view_token, current_gen).
inline bool should_adopt_result(const ViewLiveToken* result_token,
                                uint64_t result_gen,
                                const ViewLiveToken* view_token,
                                uint64_t current_gen) {
    if (!result_token || !view_token) return false;
    if (result_token != view_token) return false;        // token identity: defeats HWND recycle/ABA
    if (result_gen != current_gen) return false;          // generation: defeats within-view supersede
    if (view_token->closed.load(std::memory_order_acquire)) return false;
    return true;
}

// Pins THIS plugin module (the .wlx64 the linked code lives in) for process life,
// so a detached worker never returns into unmapped code after FreeLibrary. Idempotent.
void pin_plugin_module_once();

// Spawn a detached worker that runs parse_fn(job) -> std::unique_ptr<Result>, then
// PostMessages the raw Result* (ownership to the UI thread) via job->done_msg.
// Result is plugin-specific. The worker holds ONLY the job + parse_fn (must capture
// only copyable, COM-free data — NEVER the ViewState; there is no compile-time
// enforcement, it is the caller's responsibility). No join, ever.
template <class Result, class ParseFn>
void spawn_parse_worker(std::unique_ptr<ParseJob> job, ParseFn parse_fn) {
    pin_plugin_module_once();
    std::thread([job = std::move(job), parse_fn = std::move(parse_fn)]() mutable {
        if (g_shutting_down.load(std::memory_order_acquire)) return;
        if (job->live->generation.load(std::memory_order_acquire) != job->generation) return; // superseded
        std::unique_ptr<Result> result = parse_fn(*job);   // pure work; sets result->live, result->generation
        if (!result) return;
        if (g_shutting_down.load(std::memory_order_acquire)) return;   // result frees here, off loader lock
        if (job->live->closed.load(std::memory_order_acquire)) return; // view closed -> free locally
        Result* raw = result.release();
        if (!PostMessage(job->hwnd, job->done_msg,
                         static_cast<WPARAM>(job->generation), reinterpret_cast<LPARAM>(raw)))
            result.reset(raw);   // post failed (queue full / dead hwnd) -> reclaim + free locally
    }).detach();
}

// Synchronous bypass: parse + adopt inline (for any non-interactive caller that
// can't run a message pump). Returns the heap Result (caller adopts immediately).
// Deliberately skips the g_shutting_down / closed gates — there's no pump to drain
// and the caller must not invoke it after detach.
template <class Result, class ParseFn>
std::unique_ptr<Result> run_sync(const ParseJob& job, ParseFn parse_fn) {
    return parse_fn(job);
}

}  // namespace wlx::runtime::host
