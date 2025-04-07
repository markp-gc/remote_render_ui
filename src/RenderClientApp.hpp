// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include <PacketComms.h>
#include <nanogui/nanogui.h>

#include "ControlsForm.hpp"
#include "VideoPreviewWindow.hpp"

/// A screen containing all the application's other windows.
class RenderClientApp : public nanogui::Screen {
public:
  RenderClientApp(const nanogui::Vector2i& size, PacketMuxer& sender, PacketDemuxer& receiver);
  virtual ~RenderClientApp();

  virtual bool keyboard_event(int key, int scancode, int action, int modifiers);

  virtual void draw(NVGcontext* ctx);

private:
  PacketMuxer& sender;
  VideoPreviewWindow* preview;
  ControlsForm* form;
};
