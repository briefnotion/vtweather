#include "keys.h"

namespace vtw {

void KeyReader::push(KeyKind kind, char ch) {
    Key key;
    key.kind = kind;
    key.ch = ch;
    keys_.push_back(key);
}

void KeyReader::plain(char c) {
    if (c == 27) { state_ = State::Escaped; return; }
    if (c == '\r' || c == '\n') push(KeyKind::Enter);
    else if (c == 127 || c == 8) push(KeyKind::Backspace);
    else push(KeyKind::Char, c);
}

void KeyReader::feed(char c) {
    switch (state_) {
        case State::Plain:
            plain(c);
            break;
        case State::Escaped:
            if (c == '[' || c == 'O') {                      // the start of a key sequence such as an arrow
                state_ = State::Sequence;
            } else {                                         // the Escape was a key of its own
                state_ = State::Plain;
                push(KeyKind::Escape);
                plain(c);
            }
            break;
        case State::Sequence:
            if (c >= 0x30 && c <= 0x3F) break;               // a parameter (digits, ;): keep reading
            state_ = State::Plain;
            if (c == 'A') push(KeyKind::Up);                 // anything else (Delete, Home, F-keys...) is ignored
            else if (c == 'B') push(KeyKind::Down);
            else if (c == 'C') push(KeyKind::Right);
            else if (c == 'D') push(KeyKind::Left);
            break;
    }
}

void KeyReader::flush() {
    if (state_ == State::Escaped) {
        state_ = State::Plain;
        push(KeyKind::Escape);
    }
}

bool KeyReader::next(Key& key) {
    if (keys_.empty()) return false;
    key = keys_.front();
    keys_.pop_front();
    return true;
}

}  // namespace vtw
