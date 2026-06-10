#include <doctest/doctest.h>

#include "runtime/host/async_loader.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

using namespace wlx::runtime::host;

TEST_CASE("should_adopt_result accepts a matching token + generation") {
    ViewLiveToken tok;
    CHECK(should_adopt_result(&tok, /*result_gen*/ 5, &tok, /*current_gen*/ 5));
}

TEST_CASE("should_adopt_result rejects a mismatched token (HWND recycle / ABA)") {
    ViewLiveToken view_tok;
    ViewLiveToken result_tok;  // a different view's token reusing the same HWND
    CHECK_FALSE(should_adopt_result(&result_tok, 5, &view_tok, 5));
}

TEST_CASE("should_adopt_result rejects a mismatched generation (within-view supersede)") {
    ViewLiveToken tok;
    // Worker finished for load gen 3, but the view has since moved on to gen 4.
    CHECK_FALSE(should_adopt_result(&tok, /*result_gen*/ 3, &tok, /*current_gen*/ 4));
}

TEST_CASE("should_adopt_result rejects a closed view") {
    ViewLiveToken tok;
    tok.closed.store(true, std::memory_order_release);
    CHECK_FALSE(should_adopt_result(&tok, 5, &tok, 5));
}

TEST_CASE("should_adopt_result rejects null tokens") {
    ViewLiveToken tok;
    CHECK_FALSE(should_adopt_result(nullptr, 5, &tok, 5));
    CHECK_FALSE(should_adopt_result(&tok, 5, nullptr, 5));
    CHECK_FALSE(should_adopt_result(nullptr, 5, nullptr, 5));
}

TEST_CASE("g_load_gen bumps are strictly increasing and distinct") {
    uint64_t a = g_load_gen.fetch_add(1, std::memory_order_acq_rel) + 1;
    uint64_t b = g_load_gen.fetch_add(1, std::memory_order_acq_rel) + 1;
    CHECK(b > a);
    CHECK(a != b);
}

TEST_CASE("a token whose generation < g_load_gen is superseded") {
    ViewLiveToken tok;
    // Simulate: view loads file A (stamps gen) then file B (re-stamps).
    uint64_t gen_a = g_load_gen.fetch_add(1, std::memory_order_acq_rel) + 1;
    tok.generation.store(gen_a, std::memory_order_release);

    uint64_t gen_b = g_load_gen.fetch_add(1, std::memory_order_acq_rel) + 1;
    tok.generation.store(gen_b, std::memory_order_release);

    // A's worker carries gen_a; the token now reflects gen_b -> superseded.
    CHECK(gen_a < tok.generation.load(std::memory_order_acquire));
    // And the adoption guard would reject A's stale result against current gen_b.
    CHECK_FALSE(should_adopt_result(&tok, gen_a, &tok, gen_b));
    // While B's result is still adoptable.
    CHECK(should_adopt_result(&tok, gen_b, &tok, gen_b));
}

// ---------------------------------------------------------------------------
// Integration: real detached worker -> PostMessage -> real message-only window
// -> bounded pump -> adopt/reject -> exactly-once free. Exercises the actual
// spawn_parse_worker plumbing end-to-end (not just the pure adoption guard).
// ---------------------------------------------------------------------------
namespace {

// Counts every ~TestResult so each case can assert "freed exactly once". The
// dtor may run on the worker thread (bail paths) OR the pump thread (adopt
// path), so it must be atomic.
std::atomic<int> g_frees{0};

// Move-only-friendly fake Result carried across the thread boundary as a raw
// pointer (ownership moves to the UI thread via PostMessage's lParam).
struct TestResult {
    uint64_t generation = 0;
    std::shared_ptr<ViewLiveToken> live;
    int value = 0;
    ~TestResult() { g_frees.fetch_add(1, std::memory_order_acq_rel); }
};

// Outcome recorded by the test WndProc (runs on the pump/test thread).
struct Outcome {
    bool adopted = false;
    bool rejected = false;
    int adopted_value = 0;
};
Outcome g_outcome;

// The "view" identity the WndProc adopts against. Reset per case.
std::shared_ptr<ViewLiveToken> g_test_view_token;
uint64_t g_test_current_gen = 0;

const UINT kDone = WM_APP + 1;
const wchar_t* kTestClass = L"WlxListerineAsyncLoaderTestWnd";

LRESULT CALLBACK test_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == kDone) {
        // Take ownership FIRST so it frees on every exit path (matches the real
        // WndProc contract). Frees here on scope exit -> counted by ~TestResult.
        std::unique_ptr<TestResult> res(reinterpret_cast<TestResult*>(lParam));
        if (should_adopt_result(res->live.get(), res->generation,
                                g_test_view_token.get(), g_test_current_gen)) {
            g_outcome.adopted = true;
            g_outcome.adopted_value = res->value;
        } else {
            g_outcome.rejected = true;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Bounded message pump. Drains the queue, checks the predicate, sleeps 1ms, and
// gives up at the timeout. Returns whether the predicate ever became true.
// NEVER an unbounded GetMessage loop -> cannot hang CI.
bool pump_until(const std::function<bool()>& pred, int ms_timeout) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(ms_timeout);
    for (;;) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (pred()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        Sleep(1);
    }
}

// RAII message-only window + window class, created/destroyed per fixture.
struct TestWindow {
    HWND hwnd = nullptr;
    ATOM atom = 0;

    TestWindow() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = test_wndproc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kTestClass;
        atom = RegisterClassW(&wc);
        REQUIRE(atom != 0);
        hwnd = CreateWindowExW(0, kTestClass, L"", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        REQUIRE(hwnd != nullptr);
    }
    ~TestWindow() {
        if (hwnd) DestroyWindow(hwnd);
        if (atom) UnregisterClassW(kTestClass, GetModuleHandleW(nullptr));
    }
};

// Reset all mutable test globals so each case is independent.
void reset_globals() {
    g_frees.store(0, std::memory_order_release);
    g_outcome = Outcome{};
    g_test_view_token.reset();
    g_test_current_gen = 0;
}

}  // namespace

TEST_CASE("spawn_parse_worker: end-to-end parse -> post -> pump -> adopt -> free once") {
    reset_globals();
    TestWindow win;

    auto live = std::make_shared<ViewLiveToken>();
    live->generation = 7;
    g_test_view_token = live;
    g_test_current_gen = 7;

    auto job = std::make_unique<ParseJob>();
    job->path = L"";
    job->generation = 7;
    job->live = live;
    job->hwnd = win.hwnd;
    job->done_msg = kDone;

    spawn_parse_worker<TestResult>(std::move(job), [](const ParseJob& j) {
        auto r = std::make_unique<TestResult>();
        r->generation = j.generation;
        r->live = j.live;
        r->value = 42;
        return r;
    });

    REQUIRE(pump_until([] { return g_outcome.adopted || g_outcome.rejected; }, 3000));
    CHECK(g_outcome.adopted);
    CHECK_FALSE(g_outcome.rejected);
    CHECK(g_outcome.adopted_value == 42);
    CHECK(g_frees.load(std::memory_order_acquire) == 1);
}

TEST_CASE("spawn_parse_worker: generation supersede bails pre-parse (no post, no result built)") {
    reset_globals();
    TestWindow win;

    auto live = std::make_shared<ViewLiveToken>();
    // View has already moved on to gen 9 BEFORE the worker's first gate; the
    // job still carries gen 7 -> worker returns at the generation gate, NO post,
    // and parse_fn never runs (so no TestResult is ever allocated).
    live->generation = 9;
    g_test_view_token = live;
    g_test_current_gen = 9;

    auto job = std::make_unique<ParseJob>();
    job->path = L"";
    job->generation = 7;
    job->live = live;
    job->hwnd = win.hwnd;
    job->done_msg = kDone;

    spawn_parse_worker<TestResult>(std::move(job), [](const ParseJob& j) {
        auto r = std::make_unique<TestResult>();
        r->generation = j.generation;
        r->live = j.live;
        r->value = 42;
        return r;
    });

    // No message should arrive within a short bounded wait.
    CHECK_FALSE(pump_until([] { return g_outcome.adopted || g_outcome.rejected; }, 500));
    CHECK_FALSE(g_outcome.adopted);
    CHECK_FALSE(g_outcome.rejected);
    // parse_fn never ran -> nothing was allocated -> nothing to free.
    CHECK(g_frees.load(std::memory_order_acquire) == 0);
}

TEST_CASE("spawn_parse_worker: closed bail post-parse (result built then freed locally, once)") {
    reset_globals();
    TestWindow win;

    auto live = std::make_shared<ViewLiveToken>();
    // Gen matches (passes the generation gate) but the view is closed -> the
    // worker runs parse_fn, builds the TestResult, then bails at the closed gate
    // WITHOUT posting; the local unique_ptr frees it on the worker thread.
    live->generation = 7;
    live->closed.store(true, std::memory_order_release);
    g_test_view_token = live;
    g_test_current_gen = 7;

    auto job = std::make_unique<ParseJob>();
    job->path = L"";
    job->generation = 7;
    job->live = live;
    job->hwnd = win.hwnd;
    job->done_msg = kDone;

    spawn_parse_worker<TestResult>(std::move(job), [](const ParseJob& j) {
        auto r = std::make_unique<TestResult>();
        r->generation = j.generation;
        r->live = j.live;
        r->value = 42;
        return r;
    });

    // No message should arrive (worker bailed without posting).
    CHECK_FALSE(pump_until([] { return g_outcome.adopted || g_outcome.rejected; }, 500));
    CHECK_FALSE(g_outcome.adopted);
    CHECK_FALSE(g_outcome.rejected);
    // The built-but-unposted result is freed exactly once on the worker. The
    // worker is instant but may finish just after the pump returns, so wait
    // (bounded) for the free to land.
    pump_until([] { return g_frees.load(std::memory_order_acquire) == 1; }, 500);
    CHECK(g_frees.load(std::memory_order_acquire) == 1);
}
