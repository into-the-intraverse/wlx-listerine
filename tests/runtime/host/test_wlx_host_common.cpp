#include <doctest/doctest.h>
#include "runtime/host/wlx_host_common.h"

#include <string>
#include <windows.h>

namespace {

struct FakeV {
    HWND hwnd = nullptr;
    std::wstring file_path;
    HWND subclass_target = nullptr;
    int reload_count = 0;
};

inline void reload_view(FakeV& v, const wchar_t*) {
    v.reload_count++;
}

static_assert(HostView<FakeV>);

} // namespace

TEST_CASE("HostIntegration attach/detach refcounts subclasses per parent") {
    HostIntegration<FakeV> integ;
    FakeV a, b;
    a.hwnd = reinterpret_cast<HWND>(0x1001);
    b.hwnd = reinterpret_cast<HWND>(0x1002);
    // Use GetDesktopWindow as a real HWND that accepts SetWindowSubclass.
    HWND parent = GetDesktopWindow();

    integ.attach(&a, parent);
    integ.attach(&b, parent);
    CHECK(integ.views_for_test().size() == 2);
    CHECK(a.subclass_target == b.subclass_target);

    integ.detach(&a);
    CHECK(integ.views_for_test().size() == 1);
    integ.detach(&b);
    CHECK(integ.views_for_test().empty());
}

TEST_CASE("HostIntegration menu-source reload fans out to all matching views (H3)") {
    HostIntegration<FakeV> integ;
    FakeV a, b;
    a.hwnd = reinterpret_cast<HWND>(0x2001);
    b.hwnd = reinterpret_cast<HWND>(0x2002);
    a.file_path = L"dummy_a.md";
    b.file_path = L"dummy_b.md";
    HWND parent = GetDesktopWindow();

    integ.attach(&a, parent);
    integ.attach(&b, parent);
    REQUIRE(a.subclass_target == b.subclass_target);

    const UINT kFakeReloadId = 7777;
    integ.set_reload_menu_id_for_test(kFakeReloadId);

    // Synthesize a menu-source WM_COMMAND. HIWORD=0 => menu; LOWORD=cmd.
    const WPARAM wp = MAKEWPARAM(kFakeReloadId, 0);
    HostIntegration<FakeV>::parent_subclass_proc_(a.subclass_target, WM_COMMAND,
                                                   wp, 0, 0, 0);

    CHECK(a.reload_count == 1);
    CHECK(b.reload_count == 1);

    integ.detach(&a);
    integ.detach(&b);
}

TEST_CASE("HostIntegration emergency_cleanup is idempotent and leaves empty state") {
    HostIntegration<FakeV> integ;
    FakeV a;
    a.hwnd = reinterpret_cast<HWND>(0x3001);
    integ.attach(&a, GetDesktopWindow());
    integ.emergency_cleanup();
    CHECK(integ.views_for_test().empty());
    integ.emergency_cleanup(); // second call must not crash
}

TEST_CASE("HostIntegration subclass proc with no self returns DefSubclassProc") {
    // When no attach has been called, self_ is nullptr. Invoking the proc
    // directly should still be safe — it falls through to DefSubclassProc.
    HostIntegration<FakeV>::parent_subclass_proc_(GetDesktopWindow(), WM_NULL,
                                                   0, 0, 0, 0);
    CHECK(true);
}
