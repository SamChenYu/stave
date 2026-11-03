#include "platform.hpp"

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
