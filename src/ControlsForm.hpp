// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include <PacketComms.h>
#include <nanogui/nanogui.h>

#include <map>

#include "PacketDescriptions.hpp"

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
  std::map<std::string, PacketSubscription> subs;

  // Receive raw image:
  packets::HdrHeader hdrHeader;
  std::vector<float> hdrBuffer;
};
