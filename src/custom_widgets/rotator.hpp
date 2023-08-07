/*
    NanoGUI was developed by Wenzel Jakob <wenzel.jakob@epfl.ch>.
    The widget drawing code is based on the NanoVG demo application
    by Mikko Mononen.

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/
/**
 * This is a rotation control derived form the nanogui::ColorWheel.
 * The ColorWheel widget was originally contributed to nanogui by Dmitriy Morozov.
 */

#pragma once

#include <nanogui/widget.h>

class Rotator : public nanogui::Widget {
public:
    /**
     * Adds a Rotator to the specified parent.
     *
     * \param parent
     *     The Widget to add this Rotator to.
     *
     * \param initialValue
     *     The initial angle of the Rotator in radians (default: 0).
     */
    Rotator(nanogui::Widget *parent, float initialValue = 0.f);

    /// The callback to execute when a user changes the Rotator value.
    std::function<void(float)> callback() const { return m_callback; }

    /// Sets the callback to execute when a user changes the Rotator value.
    void set_callback(const std::function<void(float)> &callback) { m_callback = callback; }

    float value() const;

    void set_value(float value);

    /// The preferred size of this Rotator.
    virtual nanogui::Vector2i preferred_size(NVGcontext *ctx) const override;

    /// Draws the Rotator.
    virtual void draw(NVGcontext *ctx) override;

    /// Handles mouse button click events for the Rotator.
    virtual bool mouse_button_event(const nanogui::Vector2i &p, int button, bool down, int modifiers) override;

    /// Handles mouse drag events for the Rotator.
    virtual bool mouse_drag_event(const nanogui::Vector2i &p, const nanogui::Vector2i &rel, int button, int modifiers) override;
private:
    // Used to describe where the mouse is interacting
    enum Region {
        None = 0,
        InnerTriangle = 1,
        OuterCircle = 2,
        Both = 3
    };

    // Manipulates the positioning of the different regions of the Rotator.
    Region adjust_position(const nanogui::Vector2i &p, Region considered_regions = Both);

protected:
    // Tracked widget position in GUI:
    float m_value;
    // Angle value derived from the widget position:
    float m_angle;

    /// The current region the mouse is interacting with.
    Region m_drag_region;

    /// The current callback to execute when the value changes.
    std::function<void(float)> m_callback;
};
