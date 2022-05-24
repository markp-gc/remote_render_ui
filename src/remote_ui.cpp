// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include <nanogui/nanogui.h>
#include <GLFW/glfw3.h>

#include <PacketComms.h>
#include <PacketSerialisation.h>
#include <network/TcpSocket.h>

#include <chrono>
#include <iostream>

#include <boost/program_options.hpp>
#include <boost/log/trivial.hpp>

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
  );
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
    add_widget("Rotation", slider);

    add_group("Render Status");
    auto progress = new nanogui::ProgressBar(window);
    add_widget("Progress", progress);

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
      : nanogui::Screen(Vector2i(1280, 960), "InterfaceClient Gui", false) {
    using namespace nanogui;

    form = new TestForm(this, sender, receiver);

    perform_layout();
    form->center();
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
    Screen::draw(ctx);
  }

 private:
  TestForm* form;
};

int main(int argc, char** argv) {

  auto args = parseOptions(argc, argv, getOptions());

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

    const std::vector<std::string> packetTypes{"progress", "env_rotation"};
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
