#include "runtime/io/file_service.h"

#include <windows.h>

namespace wlx::runtime::io {

// Hard upper bound at this layer: refuse absurd sizes as a read failure
// instead of trying to buffer them (a DWORD-sized ReadFile couldn't anyway).
constexpr uint64_t kMaxReadBytes = 1ull << 30;  // 1 GiB

std::optional<std::string> FileService::read_bytes(const wchar_t* path, FileIdentity& out_identity) {
    out_identity.path = path;
    out_identity.size = 0;
    out_identity.mtime = 0;

    HANDLE hFile = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE)
        return std::nullopt;

    LARGE_INTEGER li{};
    if (!GetFileSizeEx(hFile, &li) || static_cast<uint64_t>(li.QuadPart) > kMaxReadBytes) {
        CloseHandle(hFile);
        return std::nullopt;
    }
    DWORD file_size = static_cast<DWORD>(li.QuadPart);

    FILETIME ft{};
    GetFileTime(hFile, nullptr, nullptr, &ft);
    out_identity.size = file_size;
    out_identity.mtime = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;

    if (file_size == 0) {
        CloseHandle(hFile);
        return std::string{};
    }

    // Read straight into the std::string we'll keep as raw_utf8 — no separate
    // byte vector + decode copy on the common (UTF-8 / no-BOM) path.
    std::string buffer;
    buffer.resize(file_size);
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(hFile, buffer.data(), file_size, &bytes_read, nullptr);
    CloseHandle(hFile);

    if (!ok || bytes_read != file_size)
        return std::nullopt;

    return buffer;
}

std::string FileService::to_utf8(std::string raw) {
    // raw holds bytes in a std::string, so its elements are (possibly signed)
    // char — compare BOM bytes through unsigned char to avoid sign extension.
    auto byte = [&](size_t i) { return static_cast<unsigned char>(raw[i]); };

    if (raw.size() >= 3 && byte(0) == 0xEF && byte(1) == 0xBB && byte(2) == 0xBF) {
        // UTF-8 BOM: strip the 3 marker bytes in place (no reallocation).
        raw.erase(0, 3);
        return raw;
    }

    if (raw.size() >= 2 && byte(0) == 0xFF && byte(1) == 0xFE) {
        // UTF-16 LE BOM
        const wchar_t* wstr = reinterpret_cast<const wchar_t*>(raw.data() + 2);
        int wlen = static_cast<int>((raw.size() - 2) / sizeof(wchar_t));
        if (wlen == 0) return {};

        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, nullptr, 0, nullptr, nullptr);
        if (utf8_len <= 0) return {};

        std::string result(utf8_len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, result.data(), utf8_len, nullptr, nullptr);
        return result;
    }

    if (raw.size() >= 2 && byte(0) == 0xFE && byte(1) == 0xFF) {
        // UTF-16 BE BOM: byte-swap pairs then convert
        size_t payload = raw.size() - 2;
        size_t wlen = payload / sizeof(wchar_t);
        if (wlen == 0) return {};

        std::vector<wchar_t> swapped(wlen);
        const uint8_t* src = reinterpret_cast<const uint8_t*>(raw.data()) + 2;
        for (size_t i = 0; i < wlen; ++i) {
            swapped[i] = static_cast<wchar_t>((src[i * 2] << 8) | src[i * 2 + 1]);
        }

        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, swapped.data(), static_cast<int>(wlen),
                                           nullptr, 0, nullptr, nullptr);
        if (utf8_len <= 0) return {};

        std::string result(utf8_len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, swapped.data(), static_cast<int>(wlen),
                           result.data(), utf8_len, nullptr, nullptr);
        return result;
    }

    // No BOM: already UTF-8 — hand back the buffer we read into, no copy.
    return raw;
}

std::wstring FileService::to_wstring(const std::string& utf8) {
    if (utf8.empty()) return {};

    // Strict: invalid sequences fail outright instead of silently becoming
    // U+FFFD — read() falls back to the ANSI code page on failure.
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                   static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) return {};

    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()),
                        result.data(), wlen);
    return result;
}

void FileService::normalize_line_endings(std::string& text) {
    // Fast path: LF-only files (the common case) have no '\r' — a single scan,
    // zero allocation, nothing to rewrite.
    if (text.find('\r') == std::string::npos)
        return;

    // Compact in place: "\r\n" -> "\n", lone "\r" -> "\n". The write cursor (w)
    // never overtakes the read cursor (r), so a forward scalar copy is safe.
    size_t w = 0;
    for (size_t r = 0; r < text.size(); ++r) {
        if (text[r] == '\r') {
            text[w++] = '\n';
            if (r + 1 < text.size() && text[r + 1] == '\n')
                ++r; // skip the '\n' after '\r'
        } else {
            text[w++] = text[r];
        }
    }
    text.resize(w);
}

std::optional<FileContent> FileService::read(const wchar_t* path) {
    FileIdentity ident;
    auto bytes = read_bytes(path, ident);

    if (!bytes)
        return std::nullopt;

    if (bytes->empty()) {
        // Genuinely empty file (failures are nullopt) — return empty content
        FileContent content;
        content.identity = std::move(ident);
        return content;
    }

    std::string utf8 = to_utf8(std::move(*bytes));
    normalize_line_endings(utf8);

    FileContent content;
    content.text = to_wstring(utf8);     // reads utf8
    if (content.text.empty() && !utf8.empty()) {
        // Not valid UTF-8 (no BOM, legacy bytes — e.g. Windows-1252): decode
        // via the system ANSI code page and regenerate raw_utf8 from the
        // decoded text so md4c/tree-sitter see valid UTF-8 bytes.
        int wlen = MultiByteToWideChar(CP_ACP, 0, utf8.data(),
                                       static_cast<int>(utf8.size()), nullptr, 0);
        if (wlen > 0) {
            content.text.assign(static_cast<size_t>(wlen), L'\0');
            MultiByteToWideChar(CP_ACP, 0, utf8.data(), static_cast<int>(utf8.size()),
                                content.text.data(), wlen);
            int u8len = WideCharToMultiByte(CP_UTF8, 0, content.text.data(), wlen,
                                            nullptr, 0, nullptr, nullptr);
            if (u8len > 0) {
                utf8.assign(static_cast<size_t>(u8len), '\0');
                WideCharToMultiByte(CP_UTF8, 0, content.text.data(), wlen,
                                    utf8.data(), u8len, nullptr, nullptr);
            }
        }
    }
    content.raw_utf8 = std::move(utf8);  // move LAST, after to_wstring consumed it
    content.identity = std::move(ident);
    return content;
}

std::optional<FileIdentity> FileService::identity(const wchar_t* path) {
    HANDLE hFile = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE)
        return std::nullopt;

    LARGE_INTEGER li{};
    if (!GetFileSizeEx(hFile, &li)) {
        CloseHandle(hFile);
        return std::nullopt;
    }

    FILETIME ft{};
    GetFileTime(hFile, nullptr, nullptr, &ft);
    CloseHandle(hFile);

    FileIdentity ident;
    ident.path = path;
    ident.size = static_cast<uint64_t>(li.QuadPart);
    ident.mtime = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return ident;
}

}  // namespace wlx::runtime::io
