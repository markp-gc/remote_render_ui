#pragma once

#include <nanogui/textbox.h>
#include <functional>
#include <chrono>

/**
 * \class StreamingTextBox streamingtextbox.h nanogui/streamingtextbox.h
 *
 * \brief A text box that streams input in real-time to a callback function
 * with a configurable character delay.
 */
class StreamingTextBox : public nanogui::TextBox {
public:
    /**
     * \brief Constructor
     *
     * \param parent The parent widget
     * \param value The initial value of the text box
     * \param charDelay Number of characters to wait before triggering the streaming callback
     * \param timeoutMs Timeout in milliseconds to trigger callback after user stops typing
     */
    StreamingTextBox(Widget *parent, const std::string &value = "", int charDelay = 1, int timeoutMs = 500);

    void set_streaming_callback(const std::function<void(const std::string &)> &callback) {
        mStreamingCallback = callback;
    }

    /// \brief Override keyboard character event to provide streaming functionality
    virtual bool keyboard_character_event(unsigned int codepoint) override;

    /// \brief Override keyboard event to handle deletions and other key presses
    virtual bool keyboard_event(int key, int scancode, int action, int modifiers) override;

    /// \brief Override draw to check for timeout
    virtual void draw(NVGcontext* ctx) override;

protected:
    /// \brief Trigger the streaming callback with the current value
    void triggerCallback();

    std::function<void(const std::string &)> mStreamingCallback;
    int mCharDelay;
    int mCharCounter;
    int mTimeoutMs;
    std::chrono::steady_clock::time_point mLastInputTime;
    bool mCallbackPending;
    std::string mLastCallbackValue;
};
