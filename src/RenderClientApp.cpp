#include "RenderClientApp.hpp"

#include <GLFW/glfw3.h>

#include <iomanip>

RenderClientApp::RenderClientApp(const nanogui::Vector2i& size, PacketMuxer& sender, PacketDemuxer& receiver)
:
  nanogui::Screen(size, "IPU Neural Render Preview", false),
  preview(nullptr),
  form(nullptr)
{
  preview = new VideoPreviewWindow(this, "Render Preview", receiver);
  form = new ControlsForm(this, sender, receiver);

  // Have to manually set positions due to bug in ComboBox:
  const int margin = 10;
  nanogui::Vector2i pos(margin, margin);
  preview->set_position(pos);
  pos[0] += margin + preview->width();
  form->set_position(nanogui::Vector2i(pos));
  perform_layout();
}

bool RenderClientApp::keyboard_event(int key, int scancode, int action, int modifiers) {
  if (Screen::keyboard_event(key, scancode, action, modifiers)) {
    return true;
  }

  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_R) {
        preview->reset();
        return true;
    }
    if (key == GLFW_KEY_ESCAPE) {
      set_visible(false);
      return true;
    }
  }

  return false;
}

void RenderClientApp::draw(NVGcontext* ctx) {
  if (preview != nullptr && form != nullptr) {
    // Update bandwidth before display:
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2)
      << preview->getVideoBandwidthMbps();
    form->bitRateText->set_value(ss.str());
  }
  Screen::draw(ctx);
}

void RenderClientApp::set_nif_selection(const ControlsForm::FileLookup& nifFileMapping) {
  form->set_nif_selection(nifFileMapping);
  perform_layout();
}
