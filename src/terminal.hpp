#pragma once
#include <memory>
#include <string>

// On Windows, turns on escape-sequence handling and UTF-8 output for the console while it exists,
// and puts the console back as it was when destroyed. Does nothing elsewhere.
class ConsoleSetup {
public:
    ConsoleSetup();
    ~ConsoleSetup();
    ConsoleSetup(const ConsoleSetup&) = delete;
    ConsoleSetup& operator=(const ConsoleSetup&) = delete;

private:
    unsigned long savedMode_ = 0;
    bool restore_ = false;
    unsigned savedCodePage_ = 0;  // 0: unchanged
};

// Writes raw bytes straight to stdout, bypassing C stdio buffering. False if writing failed.
bool writeAll(const std::string&);

// True when stdin and stdout are both a terminal, so we can ask it questions and read the answers.
bool isInteractive();

// The window size as the operating system reports it. Anything it doesn't know is 0.
struct WindowSize {
    int cols = 0, rows = 0;
    int pixelW = 0, pixelH = 0;  // the text area in pixels; Windows never reports this
};
WindowSize queryWindowSize();

// Turns off echo and line buffering on stdin, so the terminal's answers can be read as they
// arrive without appearing on screen. Puts the previous mode back when destroyed.
class RawInput {
public:
    RawInput();
    ~RawInput();
    RawInput(const RawInput&) = delete;
    RawInput& operator=(const RawInput&) = delete;

    bool ok() const;
    // Appends whatever arrives within `ms` milliseconds. False on timeout or error.
    bool read(std::string& buf, int ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
