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
      hdrHeader(packets::HdrHeader{0,0,0}),
      nifChooser(nullptr),
      saveButton(nullptr),
      preview(videoPreview)
{
  window = add_window(nanogui::Vector2i(10, 10), "Control");

  // Scene controls
  add_group("Scene Parameters");
  auto* rotationWheel = new Rotator(window);
  rotationWheel->set_callback([&](float value) {
    float angle = value/(2.f*M_PI) * 360.f;
    serialise(sender, "env_rotation", angle);
  });
  add_widget("Env NIF Rotation", rotationWheel);

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

  auto* toggleButton = new nanogui::CheckBox(window);
  toggleButton->set_caption("Show raw HDR values on zoom.");
  toggleButton->set_callback([videoPreview](bool checked) {
    if (videoPreview) {
      videoPreview->displayRawValues(checked);
    }
  });
  toggleButton->set_tooltip("If checked and raw iamge data has been received from the server then HDR"
                            " values will be displayed on zoom. " + hdrDelayNote);
  add_widget("Pixel Values", toggleButton);

  add_group("Sampling Parameters");
  samplesText = new nanogui::TextBox(window, "-");
  auto* samplesSlider = new nanogui::Slider(window);
  samplesSlider->set_tooltip("Reduce to improve frame-rate. Higher values improve preview quality but increase latency.");
  samplesSlider->set_fixed_width(250);
  samplesSlider->set_callback([&](float value) {
    samplesText->set_value(std::to_string(convertSampleValue(value)));
  });
  samplesSlider->set_final_callback([&](float value) {
    const auto s = convertSampleValue(value);
    serialise(sender, "interactive_samples", s);
    BOOST_LOG_TRIVIAL(debug) << "Sending new interactive-sample count: " << s;
  });
  samplesSlider->callback()(samplesSlider->value());
  samplesSlider->final_callback()(samplesSlider->value());
  add_widget("Interactive samples", samplesSlider);
  samplesText->set_editable(false);
  samplesText->set_units(" per step");
  samplesText->set_alignment(nanogui::TextBox::Alignment::Right);
  add_widget("Sample count:", samplesText);

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
  subs["progress"] = receiver.subscribe("progress", [progress](const ComPacket::ConstSharedPacket& packet) {
    float progressValue = 0.f;
    deserialise(packet, progressValue);
    progress->set_value(progressValue);
  });

  subs["hdr_header"] = receiver.subscribe("hdr_header", [&](const ComPacket::ConstSharedPacket& packet) {
    deserialise(packet, hdrHeader);
    hdrBuffer.resize(hdrHeader.width * hdrHeader.height * 3);
    BOOST_LOG_TRIVIAL(debug) << "Large transfer initiated: "
                             << hdrHeader.width << "x" << hdrHeader.height << " in " << hdrHeader.packets << " chunks.";
    // Need to be sure the buffer is not accessed while it is incrementally updated:
    hdrBufferMutex.lock();
  });

  subs["hdr_packet"] = receiver.subscribe("hdr_packet", [&](const ComPacket::ConstSharedPacket& packet) {
    packets::HdrPacket p;
    deserialise(packet, p);
    BOOST_LOG_TRIVIAL(trace) << "Received large transfer packet number: "
                             << p.id << " size: " << p.data.size();
    if (hdrHeader.packets > 0) {
      // Copy data into buffer:
      auto offset = p.id * p.data.size();
      std::copy(p.data.begin(), p.data.end(), hdrBuffer.data() + offset);
      if (p.id == hdrHeader.packets - 1) {
        // Final packet for current image has now been received:
        hdrHeader.packets = 0;
        preview->setRawBufferData(hdrBuffer);
        hdrBufferMutex.unlock();
        saveButton->set_enabled(true);
      }
    }
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

  subs["sample_rate"] = receiver.subscribe("sample_rate", [text2, text3](const ComPacket::ConstSharedPacket& packet) {
    packets::SampleRates rates;
    deserialise(packet, rates);
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << rates.pathRate / 1e6;
    text2->set_value(ss.str());
    ss.str(std::string());
    ss << std::fixed << std::setprecision(1) << rates.rayRate / 1e9;
    text3->set_value(ss.str());
  });

  add_group("File Manager");
  saveButton = add_button("Save PFM", [&]() {
    savePfm("image.pfm");
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

void ControlsForm::savePfm(const std::string& fileName) {
  if (!hdrBuffer.empty()) {
    // Do not want to save a partially received image:
    std::lock_guard<std::mutex> lock(hdrBufferMutex);

    // Last packet so write a PFM file:
    std::ofstream f(fileName, std::ios::binary);
    f << "PF\n";
    f << std::to_string(hdrHeader.width) << " ";
    f << std::to_string(hdrHeader.height) << "\n";
    f << "-1.0\n";
    for (auto r = hdrHeader.height - 1; r >= 0; --r) {
      auto rowStart = reinterpret_cast<char*>(hdrBuffer.data() + (r * hdrHeader.width * 3));
      f.write(rowStart, hdrHeader.width * 3 * sizeof(float));
    }
  }
}
