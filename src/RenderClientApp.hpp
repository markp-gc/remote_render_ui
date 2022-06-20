// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include <nanogui/nanogui.h>
#include <PacketComms.h>

#include "VideoPreviewWindow.hpp"
#include "ControlsForm.hpp"

/// A screen containing all the application's other windows.
class RenderClientApp : public nanogui::Screen {
public:

  RenderClientApp(const nanogui::Vector2i& size, PacketMuxer& sender, PacketDemuxer& receiver);
  virtual ~RenderClientApp();

  virtual bool keyboard_event(int key, int scancode, int action, int modifiers);

  virtual void draw(NVGcontext* ctx);

  void set_nif_selection(const ControlsForm::FileLookup& nifFileMapping);

private:
  PacketMuxer& sender;
  VideoPreviewWindow* preview;
  ControlsForm* form;
};
