#include "runtime/host/factories.h"

#include <wrl/client.h>

#include <utility>

namespace wlx::runtime::host {

namespace {
Microsoft::WRL::ComPtr<ID2D1Factory>   g_d2d;
Microsoft::WRL::ComPtr<IDWriteFactory> g_dwrite;
}

void ensure_factories() {
    if (!g_d2d) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2d.GetAddressOf());
    }
    if (!g_dwrite) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(g_dwrite.GetAddressOf()));
    }
}

ID2D1Factory*   d2d_factory()    { return g_d2d.Get(); }
IDWriteFactory* dwrite_factory() { return g_dwrite.Get(); }

void leak_factories_on_detach() {
    (void) new Microsoft::WRL::ComPtr<ID2D1Factory>(std::move(g_d2d));
    (void) new Microsoft::WRL::ComPtr<IDWriteFactory>(std::move(g_dwrite));
}

}  // namespace wlx::runtime::host
