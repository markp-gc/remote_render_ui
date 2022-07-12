// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include "ControlsForm.hpp"

#include <PacketSerialisation.h>
#include <cereal/types/string.hpp>

#include <boost/log/trivial.hpp>

#include <iomanip>

namespace {

// Struct and serialize function to telemetry
// in a single packet over comms system:
struct SamplesRates {
  float pathRate;
  float rayRate;
};

template <typename T>
void serialize(T& ar, SamplesRates& s) {
  ar(s.pathRate, s.rayRate);
}

}  // end anonymous namespace

ControlsForm::ControlsForm(nanogui::Screen* screen,
                           PacketMuxer& sender,
                           PacketDemuxer& receiver)
    : nanogui::FormHelper(screen) {
  window = add_window(nanogui::Vector2i(10, 10), "Control");

  // Scene controls
  add_group("Scene Parameters");

  auto* rotSlider = new nanogui::Slider(window);
  rotSlider->set_fixed_width(250);
  rotSlider->set_callback([&](float value) {
    serialise(sender, "env_rotation", value * 360.f);
  });
  rotSlider->set_value(0.f);
  rotSlider->callback()(rotSlider->value());
  add_widget("Env NIF Rotation", rotSlider);

  nifChooser = new nanogui::ComboBox(window, {"No NIF models available"});
  nifChooser->set_enabled(false);
  nifChooser->set_side(nanogui::Popup::Side::Left);
  nifChooser->set_tooltip("Pass a JSON file using '--nif-paths' option to enable selection.");
  nifChooser->set_callback([&](int index) {
    auto path = fileMapping.at(nifChooser->items()[index]);
    BOOST_LOG_TRIVIAL(debug) << "Sending new NIF path: " << path;
    serialise(sender, "load_nif", path);
  });
  nifChooser->set_font_size(16);
  add_widget("Choose NIF HDRI: ", nifChooser);

  // Camera controls
  add_group("Camera Parameters");
  auto* fovSlider = new nanogui::Slider(window);
  fovSlider->set_fixed_width(250);
  fovSlider->set_callback([&](float value) {
    serialise(sender, "fov", value * 360.f);
  });
  fovSlider->set_value(90.f / 360.f);
  fovSlider->callback()(fovSlider->value());
  add_widget("Field of View", fovSlider);

  // Sensor controls
  add_group("Film Parameters");
  auto* exposureSlider = new nanogui::Slider(window);
  exposureSlider->set_fixed_width(250);
  exposureSlider->set_callback([&](float value) {
    value = 4.f * (value - 0.5f);
    serialise(sender, "exposure", value);
  });
  exposureSlider->set_value(.5f);
  exposureSlider->callback()(exposureSlider->value());
  add_widget("Exposure", exposureSlider);

  auto* gammaSlider = new nanogui::Slider(window);
  gammaSlider->set_fixed_width(250);
  gammaSlider->set_callback([&](float value) {
    value = 4.f * value;
    serialise(sender, "gamma", value);
  });
  gammaSlider->set_value(2.2f / 4.f);
  gammaSlider->callback()(gammaSlider->value());
  add_widget("Gamma", gammaSlider);

  // Info/stats/status:
  add_group("Render Status");
  auto progress = new nanogui::ProgressBar(window);
  add_widget("Progress", progress);

  add_button("Stop", [screen, &sender]() {
    serialise(sender, "stop", true);
    screen->set_visible(false);
  })->set_tooltip("Stop the remote application.");

  // Make a subscriber to receive progress updates:
  // (the progress pointer needs to be captured by value).
  progressSub = receiver.subscribe("progress", [progress](const ComPacket::ConstSharedPacket& packet) {
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
  auto text2 = new nanogui::TextBox(window, "-");
  text2->set_editable(false);
  text2->set_units("Mega-paths/sec");
  text2->set_alignment(nanogui::TextBox::Alignment::Right);
  add_widget("Path-trace rate:", text2);
  auto text3 = new nanogui::TextBox(window, "-");
  text3->set_editable(false);
  text3->set_units("Giga-rays/sec");
  text3->set_alignment(nanogui::TextBox::Alignment::Right);
  add_widget("Ray-cast rate:", text3);

  sampleRateSub = receiver.subscribe("sample_rate", [text2, text3](const ComPacket::ConstSharedPacket& packet) {
    SamplesRates rates;
    deserialise(packet, rates);
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << rates.pathRate / 1e6;
    text2->set_value(ss.str());
    ss.str(std::string());
    ss << std::fixed << std::setprecision(1) << rates.rayRate / 1e9;
    text3->set_value(ss.str());
  });
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
