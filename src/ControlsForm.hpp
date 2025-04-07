// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include <PacketComms.h>
#include <nanogui/nanogui.h>

#include <map>
#include <mutex>

#include "PacketDescriptions.hpp"
#include "VideoPreviewWindow.hpp"

/// This control window sends and receives messages via a
/// PacketMuxer and PacketDemuxer to enact a remote-controlled
/// user interface.
class ControlsForm : public nanogui::FormHelper {
public:
  using FileLookup = std::map<std::string, std::string>;

  ControlsForm(nanogui::Screen* screen, PacketMuxer& sender, PacketDemuxer& receiver, VideoPreviewWindow* videoPreview);

  void set_position(const nanogui::Vector2i& pos);

  void set_nif_selection(const FileLookup& nifFileMapping);

  nanogui::TextBox* bitRateText;
  nanogui::TextBox* frameRateText;

private:
  FileLookup fileMapping;
  nanogui::Window* window;

  // We need to hold onto these pointers so that
  // subscriber callbacks can access them:
  nanogui::ComboBox* nifChooser;
  nanogui::Button* saveButton;
  nanogui::Slider* slider;
  std::map<std::string, PacketSubscription> subs;

  nanogui::TextBox* samplesText;

  // Receive raw image:
  VideoPreviewWindow* preview;
  void savePfm(const std::string& filename);
};
