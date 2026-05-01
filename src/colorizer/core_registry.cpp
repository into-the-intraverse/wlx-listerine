#define NOMINMAX
#include "core_registry.h"

#include <windows.h>
#include <filesystem>

extern HMODULE g_core_module;  // defined in dllmain.cpp

CoreRegistry& CoreRegistry::instance() {
    static std::unique_ptr<CoreRegistry> p;
    static std::once_flag once;
    std::call_once(once, [] { p.reset(new CoreRegistry()); });
    return *p;
}

std::wstring CoreRegistry::resolve_core_dir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_core_module, buf, MAX_PATH);
    if (n == 0) return L"";
    std::wstring path(buf, n);
    auto slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"" : path.substr(0, slash + 1);
}

CoreRegistry::CoreRegistry()
    : core_dir_(resolve_core_dir())
{
    std::wstring grammar_dir = core_dir_ + L"grammars";
    std::wstring theme_dir   = core_dir_ + L"themes";
    // Hardcoded "default" here is replaced by cfg_.theme / cfg_.theme_light
    // in Task 4 when CoreConfig is wired in.
    colorizer_ = std::make_unique<Colorizer>(
        grammar_dir, theme_dir, "default", "");
}

ColorizeResult CoreRegistry::colorize(const std::string& source,
                                      const std::string& language,
                                      bool dark_mode) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!colorizer_) return {};
    return colorizer_->colorize(source, language, dark_mode);
}

bool CoreRegistry::supports(const std::string& language) {
    std::lock_guard<std::mutex> lk(mu_);
    return colorizer_ && colorizer_->supports(language);
}

const HelixTheme& CoreRegistry::theme(bool dark_mode) const {
    std::lock_guard<std::mutex> lk(mu_);
    static HelixTheme empty;
    return colorizer_ ? colorizer_->theme(dark_mode) : empty;
}
