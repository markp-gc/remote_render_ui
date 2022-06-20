// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include <nanogui/nanogui.h>
#include <PacketComms.h>

#include <map>

/// This control window sends and receives messages via a
/// PacketMuxer and PacketDemuxer to enact a remote-controlled
/// user interface.
class ControlsForm : public nanogui::FormHelper {
public:

  using FileLookup = std::map<std::string, std::string>;

  ControlsForm(nanogui::Screen* screen, PacketMuxer& sender, PacketDemuxer& receiver);

  void set_position(const nanogui::Vector2i& pos);

  void set_nif_selection(const FileLookup& nifFileMapping);

  nanogui::TextBox* bitRateText;

private:
  FileLookup fileMapping;
  nanogui::Window* window;
  nanogui::ComboBox* nifChooser;
  PacketSubscription progressSub;
  PacketSubscription sampleRateSub;
};
