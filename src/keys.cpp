#include "keys.hpp"

namespace {

Key plainKey(char ch) {
    switch (ch) {
        case 'q':
        case 'Q':
        case '\x03':  // Ctrl+C
            return Key::Quit;
        case 'h': return Key::Left;
        case 'l': return Key::Right;
        case 'k': return Key::Up;
        case 'j': return Key::Down;
        case 'H': return Key::PageLeft;
        case 'L': return Key::PageRight;
        case ' ': return Key::PageDown;
        case 'g': return Key::Home;
        case 'G': return Key::End;
        case '+':
        case '=': return Key::ZoomIn;
        case '-':
        case '_': return Key::ZoomOut;
        default: return Key::Other;
    }
}

// An escape sequence: CSI (ESC [) or SS3 (ESC O), parameters, then a final letter.
Key sequenceKey(const std::string& params, char final) {
    const bool shift = params == "1;2";
    switch (final) {
        case 'A': return Key::Up;
        case 'B': return Key::Down;
        case 'C': return shift ? Key::PageRight : Key::Right;
        case 'D': return shift ? Key::PageLeft : Key::Left;
        case 'H': return Key::Home;
        case 'F': return Key::End;
        case '~':  // keys sent as ESC [ number ~
            if (params == "5") return Key::PageUp;
            if (params == "6") return Key::PageDown;
            if (params == "1" || params == "7") return Key::Home;
            if (params == "4" || params == "8") return Key::End;
            return Key::Other;
        default: return Key::Other;
    }
}

}  // namespace

Key takeKey(std::string& pending) {
    if (pending.empty()) return Key::None;
    if (pending[0] != '\x1b') {
        const char ch = pending[0];
        pending.erase(0, 1);
        return plainKey(ch);
    }
    if (pending.size() < 2) return Key::None;  // ESC alone: maybe the start of a sequence
    if (pending[1] == '\x1b') {  // ESC ESC: the first one was the Esc key on its own
        pending.erase(0, 1);
        return Key::Quit;
    }
    if (pending[1] != '[' && pending[1] != 'O') {  // Alt+key and the like
        pending.erase(0, 2);
        return Key::Other;
    }
    // Parameter bytes run until a final byte in 0x40-0x7E.
    size_t end = 2;
    while (end < pending.size() && !(pending[end] >= 0x40 && pending[end] <= 0x7E)) ++end;
    if (end >= pending.size()) return Key::None;  // the rest hasn't arrived yet
    const std::string params = pending.substr(2, end - 2);
    const char final = pending[end];
    pending.erase(0, end + 1);
    return sequenceKey(params, final);
}

Key flushPending(std::string& pending) {
    const bool loneEsc = pending == "\x1b";
    pending.clear();
    return loneEsc ? Key::Quit : Key::None;
}
