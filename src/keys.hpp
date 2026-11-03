#pragma once
#include <string>

enum class Key {
    None,   // nothing (complete) to read yet
    Other,  // a key the pager doesn't use
    Left,
    Right,
    Up,
    Down,
    PageLeft,
    PageRight,
    PageUp,
    PageDown,
    Home,
    End,
    ZoomIn,
    ZoomOut,
    Quit,
};

// Takes one key off the front of `pending`, the raw input received so far.
// Returns Key::None when it's empty, or holds only the start of an escape sequence whose rest may
// still be on its way: read more input and call again.
//
//   arrows, h j k l            Left Right Up Down
//   Shift+arrows, H L          PageLeft PageRight
//   PgUp, PgDn, Space          PageUp PageDown (Space is PageDown)
//   Home End, g G              Home End
//   + = and - _                ZoomIn ZoomOut
//   q Q, Ctrl+C                Quit
Key takeKey(std::string& pending);

// Call when no more input has arrived for a while. A lone ESC still pending was the Esc key on
// its own, which quits; anything else left over is an incomplete sequence and is dropped.
Key flushPending(std::string& pending);
