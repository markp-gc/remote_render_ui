// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include <nanogui/nanogui.h>
#include <GLFW/glfw3.h>

#include <PacketComms.h>
#include <PacketSerialisation.h>
#include <network/TcpSocket.h>
#include "VideoClient.hpp"

#include <chrono>
#include <iostream>

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
    auto* slider = new nanogui::Slider(window);
    slider->set_value(0.f);
    slider->set_fixed_width(200);
    slider->set_final_callback([&](float value) {
      serialise(sender, "env_rotation", value);
    });
    slider->set_callback([&](float value) {
      serialise(sender, "env_rotation", value);
    });
    add_widget("Rotation", slider);

    add_group("Render Status");
    auto progress = new nanogui::ProgressBar(window);
    add_widget("Progress", progress);

    add_button("Stop", [&]() {
      bool stop = true;
      serialise(sender, "stop", stop);
    })->set_tooltip("Stop the remote application.");

    // Make a subscriber to receive progress updates:
    // (the progress pointer needs to be captured by value).
    progressSub = receiver.subscribe("progress", [progress](const ComPacket::ConstSharedPacket& packet) {
      float progressValue = 0.f;
      deserialise(packet, progressValue);
      progress->set_value(progressValue);
    });
  }

  void center() {
    window->center();
  }

private:
  nanogui::Window* window;
  PacketSubscription progressSub;
};

class InterfaceClient : public nanogui::Screen {
 public:
  InterfaceClient(PacketMuxer& sender, PacketDemuxer& receiver)
      : nanogui::Screen(Vector2i(1280, 900), "InterfaceClient Gui", false),
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
      bgrBuffer.resize(w * h * 3);
      previewWindow->set_size(Vector2i(w, h));
      previewWindow->set_position(Vector2i(10, 10));
      previewWindow->set_layout(new GroupLayout(0));
      imageView = new ImageView(previewWindow);

      for (auto c = 0; c < bgrBuffer.size(); c += 3) {
        bgrBuffer[c + 0] = 255;
        bgrBuffer[c + 1] = 0;
        bgrBuffer[c + 2] = 0;
      }

      texture = new Texture(
        Texture::PixelFormat::RGB,
        Texture::ComponentFormat::UInt8,
        Vector2i(w, h),
        Texture::InterpolationMode::Trilinear,
        Texture::InterpolationMode::Nearest);
      BOOST_LOG_TRIVIAL(trace) << "Created texture with "
                               << texture->channels() << " channels, "
                               << "data ptr: " << (void*)bgrBuffer.data();
      texture->upload(bgrBuffer.data());

      imageView->set_size(Vector2i(w, h));
      imageView->set_image(texture);
      imageView->center();
      imageView->set_pixel_callback(
        [this](const Vector2i& pos, char **out, size_t size) {
          // The information provided by this callback is used to
          // display pixel values at high magnification:
          auto w = videoClient->getFrameWidth();
          std::size_t index = (pos.x() + w * pos.y()) * 3;
          for (int ch = 0; ch < 3; ++ch) {
            uint8_t value = bgrBuffer[index + ch];
            snprintf(out[ch], size, "%i", (int)value);
          }
        }
      );
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
          stream.ExtractRgbImage(bgrBuffer.data(), w * 3);
          texture->upload(bgrBuffer.data());
        }
    });

    if (gotFrame) {
      double bps = videoClient->computeVideoBandwidthConsumed();
      BOOST_LOG_TRIVIAL(debug) << "Video bit-rate: " << bps/(1024.0*1024.0) << " Mbps" << std::endl;
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

    const std::vector<std::string> packetTypes{"progress", "env_rotation", "stop", "render_preview"};
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
