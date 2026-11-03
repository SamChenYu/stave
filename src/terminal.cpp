#include "terminal.hpp"

#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
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

bool isInteractive() {
    DWORD mode;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) &&
           GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode);
}

WindowSize queryWindowSize() {
    WindowSize ws;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        ws.cols = info.srWindow.Right - info.srWindow.Left + 1;
        ws.rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
    return ws;
}

struct RawInput::Impl {
    HANDLE in = nullptr;
    DWORD saved = 0;
    bool ok = false;
};

RawInput::RawInput() : impl_(std::make_unique<Impl>()) {
    impl_->in = GetStdHandle(STD_INPUT_HANDLE);
    if (GetConsoleMode(impl_->in, &impl_->saved)) {
        // VT input: the terminal's answers arrive as characters. Without ENABLE_PROCESSED_INPUT,
        // Ctrl+C arrives as a character too instead of killing us before the mode is put back.
        impl_->ok = SetConsoleMode(impl_->in, ENABLE_VIRTUAL_TERMINAL_INPUT) != 0;
    }
}

RawInput::~RawInput() {
    if (impl_->ok) SetConsoleMode(impl_->in, impl_->saved);
}

bool RawInput::ok() const { return impl_->ok; }

bool RawInput::read(std::string& buf, int ms) {
    if (WaitForSingleObject(impl_->in, DWORD(std::max(0, ms))) != WAIT_OBJECT_0) return false;
    INPUT_RECORD recs[128];
    DWORD n = 0;
    if (!ReadConsoleInputW(impl_->in, recs, 128, &n)) return false;
    for (DWORD i = 0; i < n; ++i) {
        if (recs[i].EventType != KEY_EVENT) continue;  // focus, mouse and resize events
        const KEY_EVENT_RECORD& k = recs[i].Event.KeyEvent;
        const WCHAR ch = k.uChar.UnicodeChar;
        if (k.bKeyDown && ch != 0 && ch < 128) buf.append(std::max<WORD>(1, k.wRepeatCount), char(ch));
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

bool isInteractive() { return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO); }

WindowSize queryWindowSize() {
    WindowSize ws;
    winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        ws.cols = w.ws_col;
        ws.rows = w.ws_row;
        ws.pixelW = w.ws_xpixel;
        ws.pixelH = w.ws_ypixel;
    }
    return ws;
}

struct RawInput::Impl {
    termios saved{};
    bool ok = false;
};

RawInput::RawInput() : impl_(std::make_unique<Impl>()) {
    if (tcgetattr(STDIN_FILENO, &impl_->saved) == 0) {
        termios raw = impl_->saved;
        // No ISIG: Ctrl+C arrives as a character instead of killing us before the mode is put back.
        raw.c_lflag &= ~tcflag_t(ICANON | ECHO | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        impl_->ok = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }
}

RawInput::~RawInput() {
    if (impl_->ok) tcsetattr(STDIN_FILENO, TCSANOW, &impl_->saved);
}

bool RawInput::ok() const { return impl_->ok; }

bool RawInput::read(std::string& buf, int ms) {
    pollfd p{STDIN_FILENO, POLLIN, 0};
    if (poll(&p, 1, std::max(0, ms)) <= 0) return false;
    char tmp[256];
    const ssize_t n = ::read(STDIN_FILENO, tmp, sizeof tmp);
    if (n <= 0) return false;
    buf.append(tmp, size_t(n));
    return true;
}

#endif
