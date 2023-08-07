/*
    This is a rotation control derived form the nanogui::ColorWheel.
    The ColorWheel widget was originally contributed to nanogui by Dmitriy Morozov.

    NanoGUI was developed by Wenzel Jakob <wenzel.jakob@epfl.ch>.
    The widget drawing code is based on the NanoVG demo application
    by Mikko Mononen.

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#include <nanogui/theme.h>
#include <nanogui/opengl.h>

#include "rotator.hpp"

using namespace nanogui;

Rotator::Rotator(Widget *parent, float initialValue)
  : Widget(parent), m_drag_region(None)
{
  set_value(initialValue);
}

Vector2i Rotator::preferred_size(NVGcontext *) const {
  return {100, 100};
}

void Rotator::draw(NVGcontext *ctx) {
  Widget::draw(ctx);

  if (!m_visible)
      return;

  float x = m_pos.x(),  y = m_pos.y(),
        w = m_size.x(), h = m_size.y();

  NVGcontext* vg = ctx;

  float hue = m_value;
  NVGpaint paint;

  nvgSave(vg);

  float cx = x + w*0.5f;
  float cy = y + h*0.5f;
  float r1 = (w < h ? w : h) * 0.5f - 5.0f;
  float r0 = r1 * .75f;

  float aeps = 0.5f / r1;   // half a pixel arc length in radians (2pi cancels out).

  for (int i = 0; i < 6; i++) {
    float a0 = (float)i / 6.0f * NVG_PI * 2.0f - aeps;
    float a1 = (float)(i+1.0f) / 6.0f * NVG_PI * 2.0f + aeps;
    nvgBeginPath(vg);
    nvgArc(vg, cx,cy, r0, a0, a1, NVG_CW);
    nvgArc(vg, cx,cy, r1, a1, a0, NVG_CCW);
    nvgClosePath(vg);
    float ax = cx + cosf(a0) * (r0+r1)*0.5f;
    float ay = cy + sinf(a0) * (r0+r1)*0.5f;
    float bx = cx + cosf(a1) * (r0+r1)*0.5f;
    float by = cy + sinf(a1) * (r0+r1)*0.5f;
    paint = nvgLinearGradient(vg, ax, ay, bx, by,
                              nvgHSLA(0.f, 0.f, a0 / (NVG_PI * 2), 255),
                              nvgHSLA(0.f, 0.f, a1 / (NVG_PI * 2), 255));
    nvgFillPaint(vg, paint);
    nvgFill(vg);
  }

  nvgBeginPath(vg);
  nvgCircle(vg, cx,cy, r0-0.5f);
  nvgCircle(vg, cx,cy, r1+0.5f);
  nvgStrokeColor(vg, nvgRGBA(0,0,0,64));
  nvgStrokeWidth(vg, 1.0f);
  nvgStroke(vg);

  // Selector
  nvgSave(vg);
  nvgTranslate(vg, cx,cy);
  nvgRotate(vg, hue*NVG_PI*2);

  // Marker on
  float u = std::max(r1/50, 1.5f);
        u = std::min(u, 4.f);
  nvgStrokeWidth(vg, u);
  nvgBeginPath(vg);
  nvgRect(vg, r0-1,-2*u,r1-r0+2,4*u);
  nvgStrokeColor(vg, nvgRGBA(255,255,255,192));
  nvgStroke(vg);

  paint = nvgBoxGradient(vg, r0-3,-5,r1-r0+6,10, 2,4, nvgRGBA(0,0,0,128), nvgRGBA(0,0,0,0));
  nvgBeginPath(vg);
  nvgRect(vg, r0-2-10,-4-10,r1-r0+4+20,8+20);
  nvgRect(vg, r0-2,-4,r1-r0+4,8);
  nvgPathWinding(vg, NVG_HOLE);
  nvgFillPaint(vg, paint);
  nvgFill(vg);

  nvgRestore(vg);

  nvgRestore(vg);
}

bool Rotator::mouse_button_event(const Vector2i &p, int button, bool down,
                                  int modifiers) {
  Widget::mouse_button_event(p, button, down, modifiers);
  if (!m_enabled || button != GLFW_MOUSE_BUTTON_1) {
    return false;
  }

  if (down) {
    m_drag_region = adjust_position(p);
    return m_drag_region != None;
  } else {
    m_drag_region = None;
    return true;
  }
}

bool Rotator::mouse_drag_event(const Vector2i &p, const Vector2i &,
                                int, int) {
return adjust_position(p, m_drag_region) != None;
}

Rotator::Region Rotator::adjust_position(const Vector2i &p, Region considered_regions) {
    float x = p.x() - m_pos.x(),
          y = p.y() - m_pos.y(),
          w = m_size.x(),
          h = m_size.y();

    float cx = w * 0.5f;
    float cy = h * 0.5f;
    float r1 = (w < h ? w : h) * 0.5f - 5.0f;
    float r0 = r1 * .75f;

    x -= cx;
    y -= cy;
    float mr = std::sqrt(x*x + y*y);

    if ((considered_regions & OuterCircle) &&
      ((mr >= r0 && mr <= r1) || (considered_regions == OuterCircle))) {
      if (!(considered_regions & OuterCircle)) {
        return None;
      }
      m_value = std::atan(y / x);
      if (x < 0) {
        m_value += NVG_PI;
      }
      m_value /= 2 * NVG_PI;

      auto m_angle = std::atan2(y, x);
      // Convert angle to range [0..2Pi):
      if (m_angle < 0) {
        m_angle += 2 * NVG_PI;
      }

      if (m_callback) {
        m_callback(m_angle);
      }

      return OuterCircle;
    }

    return None;
}

float Rotator::value() const {
  return m_value;
}

void Rotator::set_value(float value) {
  m_value = std::max(0.f, value);
  m_value = std::min(m_value, 2.f * NVG_PI);
  m_angle = m_value;
}
