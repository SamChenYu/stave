#include "platform.hpp"

#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>

#include <cstdint>
#endif

std::string executableDir() {
#if defined(_WIN32)
    wchar_t path[32768];  // the longest path Windows allows
    const DWORD n = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
    if (n == 0 || n == std::size(path)) return "";
    return std::filesystem::path(path).parent_path().string();
#elif defined(__APPLE__)
    char path[4096];
    uint32_t size = sizeof path;
    if (_NSGetExecutablePath(path, &size) != 0) return "";
    return std::filesystem::path(path).parent_path().string();
#else
    std::error_code ec;
    const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? "" : p.parent_path().string();
#endif
}

std::optional<std::string> environmentVariable(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return std::nullopt;
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (!value) return std::nullopt;
    return std::string(value);
#endif
}
