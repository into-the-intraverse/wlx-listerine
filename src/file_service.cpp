#include "file_service.h"

std::vector<uint8_t> FileService::read_bytes(const wchar_t* path, FileIdentity& out_identity) {
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
        return {};

    DWORD file_size = GetFileSize(hFile, nullptr);
    if (file_size == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return {};
    }

    FILETIME ft{};
    GetFileTime(hFile, nullptr, nullptr, &ft);
    out_identity.size = file_size;
    out_identity.mtime = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;

    if (file_size == 0) {
        CloseHandle(hFile);
        return {};
    }

    std::vector<uint8_t> buffer(file_size);
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(hFile, buffer.data(), file_size, &bytes_read, nullptr);
    CloseHandle(hFile);

    if (!ok || bytes_read != file_size)
        return {};

    return buffer;
}

std::string FileService::to_utf8(const std::vector<uint8_t>& raw) {
    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        // UTF-8 BOM: skip 3 bytes
        return std::string(reinterpret_cast<const char*>(raw.data() + 3), raw.size() - 3);
    }

    if (raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
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

    if (raw.size() >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        // UTF-16 BE BOM: byte-swap pairs then convert
        size_t payload = raw.size() - 2;
        size_t wlen = payload / sizeof(wchar_t);
        if (wlen == 0) return {};

        std::vector<wchar_t> swapped(wlen);
        const uint8_t* src = raw.data() + 2;
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

    // No BOM: assume UTF-8
    return std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
}

std::wstring FileService::to_wstring(const std::string& utf8) {
    if (utf8.empty()) return {};

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                   nullptr, 0);
    if (wlen <= 0) return {};

    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        result.data(), wlen);
    return result;
}

void FileService::normalize_line_endings(std::string& text) {
    // Replace \r\n with \n
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                result += '\n';
                ++i; // skip the \n after \r
            } else {
                result += '\n'; // lone \r becomes \n
            }
        } else {
            result += text[i];
        }
    }

    text = std::move(result);
}

std::optional<FileContent> FileService::read(const wchar_t* path) {
    FileIdentity ident;
    auto bytes = read_bytes(path, ident);

    if (bytes.empty()) {
        if (ident.size > 0) {
            // File exists but read failed
            return std::nullopt;
        }
        if (ident.mtime == 0 && ident.size == 0) {
            // Could be a genuinely empty file or a failed open.
            // If mtime is 0 and size is 0, check if the file was actually opened
            // by looking at whether the path was set (read_bytes always sets it).
            // A failed CreateFileW still sets the path but mtime stays 0.
            // We need to distinguish "empty file" from "open failed".
            // Re-check: read_bytes returns {} for INVALID_HANDLE_VALUE,
            // and out_identity has mtime=0 in that case.
            // For a real empty file, mtime will be non-zero.
            if (ident.mtime == 0) {
                // Could be open failure — try to verify
                HANDLE hCheck = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hCheck == INVALID_HANDLE_VALUE)
                    return std::nullopt; // File doesn't exist or can't be opened
                CloseHandle(hCheck);
            }
        }
        // Empty file — return empty content
        FileContent content;
        content.identity = std::move(ident);
        return content;
    }

    std::string utf8 = to_utf8(bytes);
    normalize_line_endings(utf8);

    FileContent content;
    content.raw_utf8 = utf8;
    content.text = to_wstring(utf8);
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

    DWORD file_size = GetFileSize(hFile, nullptr);
    if (file_size == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return std::nullopt;
    }

    FILETIME ft{};
    GetFileTime(hFile, nullptr, nullptr, &ft);
    CloseHandle(hFile);

    FileIdentity ident;
    ident.path = path;
    ident.size = file_size;
    ident.mtime = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return ident;
}
