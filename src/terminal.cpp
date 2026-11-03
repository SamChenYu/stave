#include "terminal.hpp"

#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

#ifdef _WIN32

ConsoleSetup::ConsoleSetup() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) {  // fails when stdout isn't a console, e.g. redirected to a file
        savedMode_ = mode;
        restore_ = SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
    }
}

ConsoleSetup::~ConsoleSetup() {
    if (restore_) SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), DWORD(savedMode_));
}

bool writeAll(const std::string& s) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        DWORD n = 0;
        const DWORD want = DWORD(std::min<size_t>(left, size_t(1) << 20));  // WriteFile takes a 32-bit size
        if (!WriteFile(out, p, want, &n, nullptr) || n == 0) return false;
        p += n;
        left -= n;
    }
    return true;
}

#else  // POSIX

ConsoleSetup::ConsoleSetup() {}
ConsoleSetup::~ConsoleSetup() {}

bool writeAll(const std::string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        const ssize_t n = ::write(STDOUT_FILENO, p, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        left -= size_t(n);
    }
    return true;
}

#endif
