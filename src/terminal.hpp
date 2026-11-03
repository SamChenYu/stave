#pragma once
#include <string>

// On Windows, turns on escape-sequence handling for the console while it exists, and puts the
// console back as it was when destroyed. Does nothing elsewhere.
class ConsoleSetup {
public:
    ConsoleSetup();
    ~ConsoleSetup();
    ConsoleSetup(const ConsoleSetup&) = delete;
    ConsoleSetup& operator=(const ConsoleSetup&) = delete;

private:
    unsigned long savedMode_ = 0;
    bool restore_ = false;
};

// Writes raw bytes straight to stdout, bypassing C stdio buffering. False if writing failed.
bool writeAll(const std::string&);
