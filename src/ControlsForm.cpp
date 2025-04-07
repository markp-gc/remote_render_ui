// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include "ControlsForm.hpp"
#include "custom_widgets/rotator.hpp"

#include <PacketSerialisation.h>
#include <cereal/types/string.hpp>

#include <boost/log/trivial.hpp>

#include <iomanip>
#include <fstream>

namespace {
std::string hdrDelayNote = "Note that the HDR values are updated infrequently"
                            " so can be many seconds out of date.";
}

std::uint32_t convertSampleValue(float value) {
  // Maximum of 16 otherwise latency will be too high:
  std::uint32_t sampleCount = value * 16;
  // Must be at least 1:
  return std::max(sampleCount, 1u);
}

ControlsForm::ControlsForm(nanogui::Screen* screen,
                           PacketMuxer& sender,
                           PacketDemuxer& receiver,
                           VideoPreviewWindow* videoPreview)
    : nanogui::FormHelper(screen),
      saveButton(nullptr),
      preview(videoPreview)
{
  window = add_window(nanogui::Vector2i(10, 10), "Control");

  // Scene controls
  add_group("Custom controls");
  auto* rotationWheel = new Rotator(window);
  rotationWheel->set_callback([&](float value) {
    serialise(sender, "value", value);
  });
  add_widget("Value rotator", rotationWheel);
  rotationWheel->set_tooltip("Example custom rotator.");

  // Camera controls
  add_group("Other controls");
  slider = new nanogui::Slider(window);
  slider->set_fixed_width(250);
  slider->set_callback([&](float value) {
    serialise(sender, "value", value);
  });
  slider->set_value(0.f);
  slider->callback()(slider->value());
  add_widget("Value slider", slider);

  // Subscribe to FOV updates from the server (on start-up the server can decide the initial value):
  subs["value"] = receiver.subscribe("value", [this](const ComPacket::ConstSharedPacket& packet) {
    float value = 0.f;
    deserialise(packet, value);
    BOOST_LOG_TRIVIAL(trace) << "Received value update: " << value;
    slider->set_value(value);
  });

  // Info/stats/status:
  add_group("Status");
  auto progress = new nanogui::ProgressBar(window);
  add_widget("Progress", progress);

  add_button("Stop", [screen, &sender]() {
    serialise(sender, "stop", true);
    screen->set_visible(false);
  })->set_tooltip("Stop the remote application.");

  // Make a subscriber to receive progress updates:
  // (the progress pointer needs to be captured by value).
  subs["progress"] = receiver.subscribe("progress", [progress](const ComPacket::ConstSharedPacket& packet) {
    float progressValue = 0.f;
    deserialise(packet, progressValue);
    progress->set_value(progressValue);
  });

  add_group("Info/Stats");
  bitRateText = new nanogui::TextBox(window, "-");
  bitRateText->set_editable(false);
  bitRateText->set_units("Mbps");
  bitRateText->set_alignment(nanogui::TextBox::Alignment::Right);
  add_widget("Video rate:", bitRateText);

  frameRateText = new nanogui::TextBox(window, "-");
  frameRateText->set_editable(false);
  frameRateText->set_units("Frames/sec");
  frameRateText->set_alignment(nanogui::TextBox::Alignment::Right);
  add_widget("Frame rate:", frameRateText);

  add_group("File Manager");
  saveButton = add_button("Save image", [&]() {
    BOOST_LOG_TRIVIAL(info) << "Save not implemented";
  });
  saveButton->set_tooltip("Save raw HDR image as a portable float map (PFM). " + hdrDelayNote);
  saveButton->set_enabled(false);
}

void ControlsForm::set_position(const nanogui::Vector2i& pos) {
  window->set_position(pos);
}

void ControlsForm::set_nif_selection(const FileLookup& nifFileMapping) {
  fileMapping = nifFileMapping;
  std::vector<std::string> items;
  for (const auto& p : nifFileMapping) {
    items.push_back(p.first);
  }
  nifChooser->set_items(items);
  nifChooser->set_tooltip("Tell the remote application to load a new NIF model.");
  nifChooser->set_enabled(true);
  if (nifChooser->items().size() > 0) {
    nifChooser->set_selected_index(0);
    nifChooser->callback()(0);
  }
}