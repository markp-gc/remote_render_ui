// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include <nanogui/nanogui.h>
#include <GLFW/glfw3.h>

#include <PacketComms.h>
#include <PacketSerialisation.h>
#include <network/TcpSocket.h>
#include "VideoClient.hpp"

#include <chrono>
#include <iostream>
#include <iomanip>

#include <boost/program_options.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

boost::program_options::options_description getOptions() {
  namespace po = boost::program_options;
  po::options_description desc("Options");
  desc.add_options()
  ("help", "Show command help.")
  ("port",
   po::value<int>()->default_value(3000),
   "Port number to connect on."
  )
  ("host",
   po::value<std::string>()->default_value("localhost"),
   "Host to connect to."
  )
  ("log-level", po::value<std::string>()->default_value("info"),
  "Set the log level to one of the following: 'trace', 'debug', 'info', 'warn', 'err', 'critical', 'off'.")
  ;
  return desc;
}

boost::program_options::variables_map
parseOptions(int argc, char** argv, boost::program_options::options_description&& desc) {
  namespace po = boost::program_options;

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  if (vm.count("help")) {
    std::cout << desc << "\n";
    throw std::runtime_error("Show help");
  }

  po::notify(vm);
  return vm;
}

using nanogui::ref;
using nanogui::Shader;
using nanogui::Vector2i;
using nanogui::Vector3f;

// Define Cereal support for nanogui types so that we can
// send/receive them directly over packetcomms system.
namespace nanogui {
template <class T>
void serialize(T& archive, Vector2i& p) {
  archive(p[0], p[1]);
}
}  // namespace nanogui

class TestForm : public nanogui::FormHelper {
public:
  TestForm(nanogui::Screen* screen, PacketMuxer& sender, PacketDemuxer& receiver)
      : nanogui::FormHelper(screen) {
    window = add_window(nanogui::Vector2i(10, 10), "Control");

    add_group("Scene Parameters");
    auto* rotSlider = new nanogui::Slider(window);
    rotSlider->set_fixed_width(250);
    rotSlider->set_callback([&](float value) {
      serialise(sender, "env_rotation", value * 360.f);
    });
    rotSlider->set_value(0.f);
    rotSlider->callback()(rotSlider->value());
    add_widget("Env NIF Rotation", rotSlider);

    add_group("Camera Parameters");
    auto* fovSlider = new nanogui::Slider(window);
    fovSlider->set_fixed_width(250);
    fovSlider->set_callback([&](float value) {
      serialise(sender, "fov", value * 360.f);
    });
    fovSlider->set_value(90.f / 360.f);
    fovSlider->callback()(fovSlider->value());
    add_widget("Field of View", fovSlider);

    add_group("Film Parameters");
    auto* gammaSlider = new nanogui::Slider(window);
    gammaSlider->set_fixed_width(250);
    gammaSlider->set_callback([&](float value) {
      value = 4.f * value;
      serialise(sender, "gamma", value);
    });
    gammaSlider->set_value(2.2f / 4.f);
    gammaSlider->callback()(gammaSlider->value());
    add_widget("Gamma", gammaSlider);

    add_group("Render Status");
    auto progress = new nanogui::ProgressBar(window);
    add_widget("Progress", progress);

    add_button("Stop", [screen, &sender]() {
      bool stop = true;
      serialise(sender, "stop", stop);
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
    add_widget("Sample rate:", text2);

    sampleRateSub = receiver.subscribe("sample_rate", [text2](const ComPacket::ConstSharedPacket& packet) {
      float rate = 0.f;
      deserialise(packet, rate);
      std::stringstream ss;
      ss << std::fixed << std::setprecision(1) << rate/1e6;
      text2->set_value(ss.str());
    });
  }

  void center() {
    window->center();
  }

  nanogui::TextBox* bitRateText;

private:
  nanogui::Window* window;
  PacketSubscription progressSub;
  PacketSubscription sampleRateSub;
};

class InterfaceClient : public nanogui::Screen {
 public:
  InterfaceClient(PacketMuxer& sender, PacketDemuxer& receiver)
      : nanogui::Screen(Vector2i(1280, 900), "IPU Neural Render Preview", false),
        texture(nullptr) {
    using namespace nanogui;

    videoClient.reset(new VideoClient(receiver, "render_preview"));
    using namespace std::chrono_literals;

    set_layout(new BoxLayout(Orientation::Horizontal, Alignment::Minimum, 0, 10));
    auto previewWindow = new Window(this, "Render Preview");

    bool videoOk = videoClient->initialiseVideoStream(2s);

    if (videoOk) {
      // Allocate a buffer to store the decoded and converted images:
      auto w = videoClient->getFrameWidth();
      auto h = videoClient->getFrameHeight();

      // Create the texture first because internally nanogui will create
      // one with the preferred format and we need to know how to allcoate
      // the buffers:
      texture = new Texture(
        Texture::PixelFormat::RGB,
        Texture::ComponentFormat::UInt8,
        Vector2i(w, h),
        Texture::InterpolationMode::Trilinear,
        Texture::InterpolationMode::Nearest);
      const auto ch = texture->channels();
      BOOST_LOG_TRIVIAL(trace) << "Created texture with "
                               << texture->channels() << " channels, "
                               << "data ptr: " << (void*)bgrBuffer.data();
      if (!(ch == 3 || ch == 4)) {
        throw std::logic_error("Texture returned has an unsupported number of texture channels.");
      }

      bgrBuffer.resize(w * h * ch);
      previewWindow->set_size(Vector2i(w, h));
      previewWindow->set_position(Vector2i(10, 10));
      previewWindow->set_layout(new GroupLayout(0));
      imageView = new ImageView(previewWindow);

      for (auto c = 0; c < bgrBuffer.size(); c += ch) {
        bgrBuffer[c + 0] = 255;
        bgrBuffer[c + 1] = 0;
        bgrBuffer[c + 2] = 0;
        if (ch == 4) {
          bgrBuffer[c + 3] = 128;
        }
      }

      texture->upload(bgrBuffer.data());

      imageView->set_size(Vector2i(w, h));
      imageView->set_image(texture);
      imageView->center();
      imageView->set_pixel_callback(
        [this](const Vector2i& pos, char **out, size_t size) {
          // The information provided by this callback is used to
          // display pixel values at high magnification:
          auto w = videoClient->getFrameWidth();
          std::size_t index = (pos.x() + w * pos.y()) * texture->channels();
          for (int c = 0; c < texture->channels(); ++c) {
            uint8_t value = bgrBuffer[index + c];
            snprintf(out[c], size, "%i", (int)value);
          }
        }
      );
      BOOST_LOG_TRIVIAL(info) << "Succesfully initialised video stream.";
    } else {
      BOOST_LOG_TRIVIAL(warning) << "Failed to initialise video stream.";
    }

    form = new TestForm(this, sender, receiver);

    perform_layout();
  }

  virtual bool keyboard_event(int key, int scancode, int action, int modifiers) {
    if (Screen::keyboard_event(key, scancode, action, modifiers))
      return true;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
      set_visible(false);
      return true;
    }
    return false;
  }

  virtual void draw(NVGcontext* ctx) {
    auto gotFrame = videoClient->receiveVideoFrame(
      [this](LibAvCapture& stream) {
        BOOST_LOG_TRIVIAL(debug) << "Decoded video frame";
        auto w = stream.GetFrameWidth();
        auto h = stream.GetFrameHeight();
        if (texture != nullptr) {
          if (texture->channels() == 3) {
            stream.ExtractRgbImage(bgrBuffer.data(), w * texture->channels());
          } else if (texture->channels() == 4) {
            stream.ExtractRgbaImage(bgrBuffer.data(), w * texture->channels());
          } else {
            throw std::runtime_error("Unsupported number of texture channels");
          }
          texture->upload(bgrBuffer.data());
        }
    });

    if (gotFrame) {
      double bps = videoClient->computeVideoBandwidthConsumed();
      double mbps = bps/(1024.0*1024.0);
      BOOST_LOG_TRIVIAL(debug) << "Video bit-rate: " << mbps << " Mbps" << std::endl;
      std::stringstream ss;
      ss << std::fixed << std::setprecision(2) << mbps;
      form->bitRateText->set_value(ss.str());
    }

    Screen::draw(ctx);
  }

 private:
  TestForm* form;
  std::unique_ptr<VideoClient> videoClient;
  std::vector<std::uint8_t> bgrBuffer;
  nanogui::Texture* texture;
  nanogui::ImageView* imageView;
};

int main(int argc, char** argv) {

  auto args = parseOptions(argc, argv, getOptions());

  namespace logging = boost::log;
  auto logLevel = args.at("log-level").as<std::string>();
  std::stringstream ss(logLevel);
  logging::trivial::severity_level level;
  ss >> level;
  logging::core::get()->set_filter(logging::trivial::severity >= level);

  try {
    // Create comms system:
    using namespace std::chrono_literals;

    auto host = args.at("host").as<std::string>();
    auto port = args.at("port").as<int>();
    auto socket    = std::make_unique<TcpSocket>();
    bool connected = socket->Connect(host.c_str(), port);
    if (!connected) {
      BOOST_LOG_TRIVIAL(info) << "Could not conect to server " << host << ":" << port;
      throw std::runtime_error("Unable to connect");
    }
    BOOST_LOG_TRIVIAL(info) << "Connected to server " << host << ":" << port;

    const std::vector<std::string> packetTypes{"progress", "env_rotation", "stop", "render_preview", "gamma", "sample_rate", "fov"};
    auto sender = std::make_unique<PacketMuxer>(*socket, packetTypes);
    auto receiver = std::make_unique<PacketDemuxer>(*socket, packetTypes);

    nanogui::init();

    {
      nanogui::ref<InterfaceClient> app = new InterfaceClient(*sender, *receiver);
      app->draw_all();
      app->set_visible(true);
      nanogui::mainloop(1 / 60.f * 1000);
    }

    nanogui::shutdown();

    // Cleanly terminate the connection:
    sender.reset();
    socket.reset();

  } catch (const std::runtime_error& e) {
    std::string error_msg = std::string("Error: ") + std::string(e.what());
    BOOST_LOG_TRIVIAL(error) << error_msg << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
