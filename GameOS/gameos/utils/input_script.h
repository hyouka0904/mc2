// SPDX-License-Identifier: GPL-3.0-only
#ifndef GAMEOS_INPUT_SCRIPT_H
#define GAMEOS_INPUT_SCRIPT_H

// Scripted input for automated tests (command line -input-script <file>).
//
// One event per line, '#' starts a comment:
//
//   <frame> move <x> <y>          put the mouse cursor at window pixel x,y
//   <frame> down <button>         press  left | middle | right
//   <frame> up <button>           release
//   <frame> click <button>        press now, release two frames later
//   <frame> keydown <key>         key names as SDL spells them: Escape,
//   <frame> keyup <key>           Return, Space, A, F1, Left, ...
//   <frame> key <key>             press now, release two frames later
//
// While a script is active the real mouse and keyboard are ignored, so a run
// does not depend on where the host's pointer happens to be.

#include <stdint.h>
#include <string>
#include <vector>

namespace input_script {

enum class Type { Move, ButtonDown, ButtonUp, KeyDown, KeyUp };

struct Event {
    uint32_t frame = 0;
    Type type = Type::Move;
    int x = 0, y = 0;   // Move
    int button = 0;     // 0 left, 1 middle, 2 right
    std::string key;    // KeyDown / KeyUp
};

// Events are returned sorted by frame (stable for equal frames). On error
// returns false and sets *error to "line N: reason".
bool parse(const std::string& text, std::vector<Event>& out, std::string* error);
bool load_file(const std::string& path, std::vector<Event>& out, std::string* error);

// Hands out the events of each frame in order.
class Player {
public:
    explicit Player(std::vector<Event> events) : events_(std::move(events)) {}
    // Events scheduled for `frame` (and any earlier ones not yet returned).
    std::vector<Event> take(uint32_t frame);
    bool finished() const { return next_ >= events_.size(); }
    bool empty() const { return events_.empty(); }

private:
    std::vector<Event> events_;
    size_t next_ = 0;
};

} // namespace input_script

#endif
