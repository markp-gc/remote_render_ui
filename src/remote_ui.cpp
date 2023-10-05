// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include <nanogui/nanogui.h>

#include <PacketComms.h>
#include <network/TcpSocket.h>

#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "ControlsForm.hpp"
#include "RenderClientApp.hpp"
#include "VideoPreviewWindow.hpp"
#include "PacketDescriptions.hpp"

boost::program_options::options_description getOptions() {
  namespace po = boost::program_options;
  po::options_description desc("Options");
  desc.add_options()
  ("help", "Show command help.")
  ("port", po::value<int>()->default_value(3000), "Port number to connect on.")
  ("host", po::value<std::string>()->default_value("localhost"), "Host to connect to.")
  ("log-level", po::value<std::string>()->default_value("info"), "Set the log level to one of the following: 'trace', 'debug', 'info', 'warn', 'err', 'critical', 'off'.")
  ("nif-paths", po::value<std::string>()->default_value(""), "JSON file containing a mapping from menu names to paths to NIF models on the remote. Used to build the NIF selection menu.")
  ("width,w", po::value<int>()->default_value(1320), "Main window width in pixels.")
  ("height,h", po::value<int>()->default_value(800), "Main window height in pixels.");
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

std::map<std::string, std::string>
jsonFileToMap(const std::string& file) {
  std::map<std::string, std::string> m;

  using boost::property_tree::ptree;
  using boost::property_tree::read_json;
  using boost::property_tree::write_json;
  ptree pt;
  read_json(file, pt);

  for (const auto& p : pt) {
    const std::string& name = p.first;
    auto path = p.second.get<std::string>("");
    m.insert(std::make_pair(name, path));
    BOOST_LOG_TRIVIAL(debug) << "Loaded NIF entry. Name: '" << name << "' remote-path: '" << path << "'";
  }

  return m;
}

int main(int argc, char** argv) {
  auto args = parseOptions(argc, argv, getOptions());

  namespace logging = boost::log;
  auto logLevel = args.at("log-level").as<std::string>();
  std::stringstream ss(logLevel);
  logging::trivial::severity_level level;
  ss >> level;
  logging::core::get()->set_filter(logging::trivial::severity >= level);

  try {
    // Parse NIF description before attempting to connect:
    std::map<std::string, std::string> remoteNifModels;
    auto nifPathJsonFile = args.at("nif-paths").as<std::string>();
    if (!nifPathJsonFile.empty()) {
      remoteNifModels = jsonFileToMap(nifPathJsonFile);
    }

    // Create comms system:
    using namespace std::chrono_literals;

    auto host = args.at("host").as<std::string>();
    auto port = args.at("port").as<int>();
    auto socket = std::make_unique<TcpSocket>();
    bool connected = socket->Connect(host.c_str(), port);
    if (!connected) {
      BOOST_LOG_TRIVIAL(info) << "Could not conect to server " << host << ":" << port;
      throw std::runtime_error("Unable to connect");
    }
    BOOST_LOG_TRIVIAL(info) << "Connected to server " << host << ":" << port;

    auto sender = std::make_unique<PacketMuxer>(*socket, packets::packetTypes);
    auto receiver = std::make_unique<PacketDemuxer>(*socket, packets::packetTypes);

    nanogui::init();

    {
      const auto w = args.at("width").as<int>();
      const auto h = args.at("height").as<int>();
      nanogui::Vector2i screenSize(w, h);
      RenderClientApp app(screenSize, *sender, *receiver);
      app.draw_all();
      app.set_visible(true);
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
