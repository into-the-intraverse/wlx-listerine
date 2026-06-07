#include <doctest/doctest.h>

#include "runtime/host/async_loader.h"

#include <memory>

using namespace wlx::runtime::host;

namespace {

// A minimal fake Result that mirrors what a real plugin Result must carry so
// run_sync's plumbing can be exercised without any COM / Direct2D.
struct FakeResult {
    int parsed_value = 0;
    std::shared_ptr<ViewLiveToken> live;
    uint64_t generation = 0;
};

}  // namespace

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

TEST_CASE("run_sync returns the parsed result inline") {
    auto live = std::make_shared<ViewLiveToken>();
    ParseJob job;
    job.path = L"C:\\fake\\doc.md";
    job.generation = 42;
    job.live = live;

    auto result = run_sync<FakeResult>(job, [](const ParseJob& j) {
        auto r = std::make_unique<FakeResult>();
        r->parsed_value = static_cast<int>(j.generation);
        r->live = j.live;
        r->generation = j.generation;
        return r;
    });

    REQUIRE(result != nullptr);
    CHECK(result->parsed_value == 42);
    CHECK(result->generation == 42);
    CHECK(result->live.get() == live.get());
}
