#pragma once
#include <optional>
#include <string>

// The folder this program's executable is in, or empty if the OS won't say.
std::string executableDir();

// An environment variable's value, or nothing if it isn't set.
std::optional<std::string> environmentVariable(const char* name);
