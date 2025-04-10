#include <GLFW/glfw3.h>
#include "StreamingTextBox.hpp"

using namespace nanogui;

StreamingTextBox::StreamingTextBox(Widget *parent, const std::string &value, int charDelay, int timeoutMs)
    : TextBox(parent, value),
      mCharDelay(charDelay > 0 ? charDelay : 1),
      mCharCounter(0),
      mTimeoutMs(timeoutMs > 0 ? timeoutMs : 500),
      mLastInputTime(std::chrono::steady_clock::now()),
      mCallbackPending(false),
      mLastCallbackValue(value) {
    // Set editable by default
    set_editable(true);
}

void StreamingTextBox::triggerCallback() {
    if (mStreamingCallback && m_value_temp != mLastCallbackValue) {
        mStreamingCallback(m_value_temp);
        mLastCallbackValue = m_value_temp;
    }
    mCharCounter = 0;
    mCallbackPending = false;
}

bool StreamingTextBox::keyboard_character_event(unsigned int codepoint) {
    // First let the parent class handle the character input
    bool result = TextBox::keyboard_character_event(codepoint);

    if (result) {
        // Update the last input time
        mLastInputTime = std::chrono::steady_clock::now();
        mCallbackPending = true;

        // Increment character counter
        mCharCounter++;

        // Check if we've reached the character delay
        if (mCharCounter >= mCharDelay) {
            triggerCallback();
        }
    }

    return result;
}

bool StreamingTextBox::keyboard_event(int key, int scancode, int action, int modifiers) {
    // Store the previous value to detect changes
    std::string prevValue = m_value_temp;

    // Let the parent class handle the keyboard event
    bool result = TextBox::keyboard_event(key, scancode, action, modifiers);

    // Check if the value has changed (e.g., due to deletion)
    if (result && prevValue != m_value_temp) {
        // Update the last input time
        mLastInputTime = std::chrono::steady_clock::now();
        mCallbackPending = true;

        // For deletion events, we want to stream immediately
        if (key == GLFW_KEY_BACKSPACE || key == GLFW_KEY_DELETE) {
            triggerCallback();
        }
        // For paste events (Ctrl+V)
        else if (key == GLFW_KEY_V && modifiers == SYSTEM_COMMAND_MOD) {
            triggerCallback();
        }
        // For Enter key, trigger immediately
        else if (key == GLFW_KEY_ENTER) {
            triggerCallback();
        }
    }

    return result;
}

void StreamingTextBox::draw(NVGcontext* ctx) {
    // Call the parent draw method
    TextBox::draw(ctx);

    // Check if we need to trigger a timeout callback
    if (mCallbackPending) {
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - mLastInputTime).count();

        if (elapsedMs >= mTimeoutMs) {
            triggerCallback();
        }
    }
}
