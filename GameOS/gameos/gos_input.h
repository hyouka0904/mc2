#ifndef GOS_INPUT_H
#define GOS_INPUT_H

namespace input {

enum KeyState {
    KS_FREE = 0, // in not pressed state
    KS_PRESSED, // just pressed
    KS_HELD,    // in pressed state 
    KS_RELEASED // just released
};

struct MouseInfo {
    MouseInfo();

    static const int NUM_BUTTONS = 5;
    float x_;
    float y_;
    float rel_x_;
    float rel_y_;
    float wheel_vert_;
    float wheel_hor_;
    KeyState button_state_[NUM_BUTTONS];
};

struct KeyboardInfo {
    KeyboardInfo();
    bool key_pressed_;
    bool key_released_;
    SDL_Keysym pressed_keysym_;
    SDL_Keysym released_keysym_;
    uint32_t first_pressed_;
    uint8_t last_state_[512];
};

const MouseInfo* getMouseInfo();
const KeyboardInfo* getKeyboardInfo();

void handleMouseMotion(const SDL_Event* event);
void handleMouseButton(const SDL_Event* event);
void handleMouseWheel(const SDL_Event* event);
void beginUpdateMouseState();
void updateMouseState();

void handleKeyEvent(const SDL_Event* event);
void updateKeyboardState();

void resetKeypress();

// Scripted input for automated tests (-input-script). While enabled, the real
// mouse position, mouse buttons and keyboard are ignored and these calls
// define the state instead. Take effect on the next update*State().
void setScriptedInput(bool enabled);
bool isScriptedInput();
void scriptedMouseMove(int x, int y);
void scriptedMouseButton(int index, bool down); // 0 left, 1 middle, 2 right
void scriptedKey(int scancode, bool down);

}

#endif //GOS_INPUT_H
