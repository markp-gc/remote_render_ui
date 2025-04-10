// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include "ControlsForm.hpp"
#include "custom_widgets/rotator.hpp"

#include <PacketSerialisation.h>
#include <cereal/types/string.hpp>
#include <nanogui/icons.h>
#include <boost/log/trivial.hpp>

#include <iomanip>
#include <fstream>
#include <string>

#include <opencv2/highgui.hpp>

ControlsForm::ControlsForm(nanogui::Screen* screen,
                           PacketMuxer& sender,
                           PacketDemuxer& receiver,
                           VideoPreviewWindow* videoPreview)
    : nanogui::FormHelper(screen),
      saveButton(nullptr),
      preview(videoPreview),
      isPlaying(true)
{
  window = add_window(nanogui::Vector2i(10, 10), "Control");

  add_group("Guidance controls");

  slider = new nanogui::Slider(window);
  slider->set_fixed_width(250);
  slider->set_callback([&](float value) {
    value *= 10.f; // change range to (0..10)
    serialise(sender, "value", value);
  });
  slider->set_value(4.5f);
  slider->callback()(slider->value());
  add_widget("Guidance scale", slider);

  promptBox = new StreamingTextBox(window, "", 2, 1000);
  promptBox->set_placeholder("Enter prompt...");
  promptBox->set_editable(true);
  promptBox->set_streaming_callback([&](const std::string& prompt) {
    serialise(sender, "prompt", prompt);
  });
  add_widget("Prompt", promptBox);

  subs["value"] = receiver.subscribe("value", [this](const ComPacket::ConstSharedPacket& packet) {
    float value = 0.f;
    deserialise(packet, value);
    BOOST_LOG_TRIVIAL(trace) << "Received value update: " << value;
    slider->set_value(value);
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

  saveButton = add_button("Save image", [this]() {
    saveImage();
  });
  saveButton->set_tooltip("Save preview image locally.");

  add_group("Server Commands");

  add_button("Stop server", [screen, &sender]() {
    serialise(sender, "stop", true);
    screen->set_visible(false);
  })->set_tooltip("Stop the remote application.");

  playPauseButton = new nanogui::ToolButton(window, isPlaying ? FA_PAUSE : FA_PLAY);
  playPauseButton->set_callback([&]() {
      isPlaying = !isPlaying;
      playPauseButton->set_icon(isPlaying ? FA_PAUSE : FA_PLAY);
      serialise(sender, "playback_state", isPlaying);
  });
  playPauseButton->set_tooltip(isPlaying ? "Pause" : "Play");
  add_widget("Play/Pause", playPauseButton);

}

void ControlsForm::set_position(const nanogui::Vector2i& pos) {
  window->set_position(pos);
}

void ControlsForm::saveImage() const {
  const std::string fn = "preview.png";
  BOOST_LOG_TRIVIAL(info) << "Saving image as " << fn;
  cv::imwrite(fn, preview->getImage());
}
