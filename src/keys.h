// KeyReader: turns the characters the terminal sends into keys. Most keys are one character, but an
// arrow key arrives as a short sequence (Escape, [ or O, and a letter, such as ESC [ A for up), and
// Enter and Backspace come as different characters on different terminals. A lone Escape and the
// start of an arrow sequence look the same until the next character shows up, so call flush() after
// the characters of one frame have been fed: an Escape that nothing followed is then a real Escape.
#pragma once

#include <deque>

namespace vtw {

enum class KeyKind { None, Char, Up, Down, Left, Right, Escape, Enter, Backspace };

struct Key {
    KeyKind kind = KeyKind::None;
    char ch = 0;                 // the character, for KeyKind::Char
};

class KeyReader {
public:
    void feed(char c);                 // one character from the terminal
    void flush();                      // the end of this frame's characters
    bool next(Key& key);               // the next complete key, if there is one

private:
    enum class State { Plain, Escaped, Sequence };
    void push(KeyKind kind, char ch = 0);
    void plain(char c);

    State state_ = State::Plain;
    std::deque<Key> keys_;
};

}  // namespace vtw
