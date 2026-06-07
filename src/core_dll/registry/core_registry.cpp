#define NOMINMAX
#include "core_dll/registry/core_registry.h"

#include <windows.h>

extern HMODULE g_core_module;  // defined at global scope in dllmain.cpp

namespace wlx::core::registry {

using namespace wlx::core::colorizer;
using namespace wlx::core::theme;

CoreRegistry& CoreRegistry::instance() {
    static std::unique_ptr<CoreRegistry> p;
    static std::once_flag once;
    std::call_once(once, [] { p.reset(new CoreRegistry()); });
    return *p;
}

std::wstring CoreRegistry::resolve_core_dir() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(g_core_module, buf.data(),
                                     static_cast<DWORD>(buf.size()));
        if (n == 0) return L"";
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        // Truncated: grow and retry. Sanity ceiling at 32768 wchars
        // (Windows long-path absolute limit).
        buf.resize(buf.size() * 2);
        if (buf.size() > 32768) return L"";
    }
    auto slash = buf.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"" : buf.substr(0, slash + 1);
}

CoreRegistry::CoreRegistry()
    : core_dir_(resolve_core_dir())
    , cfg_(CoreConfig::load(core_dir_))
{
    std::wstring grammar_dir = core_dir_ + L"grammars";
    std::wstring theme_dir   = core_dir_ + L"themes";
    colorizer_ = std::make_unique<Colorizer>(
        grammar_dir, theme_dir, cfg_.theme, cfg_.theme_light,
        cfg_.cap, cfg_.ttl_minutes);
}

ColorizeResult CoreRegistry::colorize(std::string_view source,
                                      const std::string& language,
                                      bool dark_mode,
                                      uint32_t range_start,
                                      uint32_t range_end) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!colorizer_) return {};
    return colorizer_->colorize(source, language, dark_mode, range_start, range_end);
}

bool CoreRegistry::supports(const std::string& language) {
    std::lock_guard<std::mutex> lk(mu_);
    return colorizer_ && colorizer_->supports(language);
}

void CoreRegistry::prewarm(const std::string& language) {
    std::lock_guard<std::mutex> lk(mu_);
    if (colorizer_) colorizer_->prewarm(language);
}

const HelixTheme& CoreRegistry::theme(bool dark_mode) const {
    std::lock_guard<std::mutex> lk(mu_);
    static HelixTheme empty;
    return colorizer_ ? colorizer_->theme(dark_mode) : empty;
}

std::vector<std::string> CoreRegistry::available_languages() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (!colorizer_) return {};
    return colorizer_->available_languages();
}

}  // namespace wlx::core::registry
