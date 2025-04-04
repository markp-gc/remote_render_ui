#include <PacketComms.h>
#include <PacketSerialisation.h>
#include <VideoLib.h>
#include <network/TcpSocket.h>
#include <PacketDescriptions.hpp>

#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <opencv2/imgproc.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/program_options.hpp>

#include <atomic>
#include <chrono>
#include <iostream>

boost::program_options::options_description getOptions() {
  namespace po = boost::program_options;
  po::options_description desc("Options");
  desc.add_options()
  ("help", "Show command help.")
  ("port", po::value<int>()->default_value(4242), "Port to listen for connections on.")
  ("log-level", po::value<std::string>()->default_value("debug"), "Set the log level to one of the following: 'trace', 'debug', 'info', 'warn', 'err', 'critical', 'off'.");
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

namespace {

// Struct and serialize function to send
// telemetry in a single packet:
struct SampleRates {
  float pathRate;
  float rayRate;
};

template <typename T>
void serialize(T& ar, SampleRates& s) {
  ar(s.pathRate, s.rayRate);
}

}  // end anonymous namespace

using namespace std::chrono_literals;

class InterfaceServer {

  // Set up communication channels and subscriptions and then enter a transmit/receive loop.
  void communicate() {
    BOOST_LOG_TRIVIAL(info) << "User interface server listening on port " << port;
    serverSocket.Bind(port);
    serverSocket.Listen(0);
    connection = serverSocket.Accept();
    if (connection) {
      BOOST_LOG_TRIVIAL(info) << "User interface client connected.";
      connection->setBlocking(false);
      PacketDemuxer receiver(*connection, packets::packetTypes);
      sender.reset(new PacketMuxer(*connection, packets::packetTypes));

      syncWithClient(*sender, receiver, "ready");

      // Lambda that enqueues video packets via the Muxing system:
      FFMpegStdFunctionIO videoIO(FFMpegCustomIO::WriteBuffer, [&](uint8_t* buffer, int size) {
        if (sender) {
          BOOST_LOG_TRIVIAL(debug) << "Sending compressed video packet of size: " << size;
          sender->emplacePacket("render_preview", reinterpret_cast<VectorStream::CharType*>(buffer), size);
          return sender->ok() ? size : -1;
        }
        return -1;
      });
      videoStream.reset(new LibAvWriter(videoIO));

      auto subs1 = receiver.subscribe("env_rotation",
                                      [&](const ComPacket::ConstSharedPacket& packet) {
                                        deserialise(packet, state.envRotationDegrees);
                                        BOOST_LOG_TRIVIAL(trace) << "Env rotation new value: " << state.envRotationDegrees;
                                        stateUpdated = true;
                                      });

      auto subs2 = receiver.subscribe("detach",
                                      [&](const ComPacket::ConstSharedPacket& packet) {
                                        deserialise(packet, state.detach);
                                        BOOST_LOG_TRIVIAL(trace) << "Remote UI detached.";
                                        stateUpdated = true;
                                      });

      auto subs3 = receiver.subscribe("stop",
                                      [&](const ComPacket::ConstSharedPacket& packet) {
                                        deserialise(packet, state.stop);
                                        BOOST_LOG_TRIVIAL(trace) << "Render stopped by remote UI.";
                                        stateUpdated = true;
                                      });

      auto subs4 = receiver.subscribe("exposure",
                                      [&](const ComPacket::ConstSharedPacket& packet) {
                                        deserialise(packet, state.exposure);
                                        BOOST_LOG_TRIVIAL(trace) << "Exposure new value: " << state.exposure;
                                        stateUpdated = true;
                                      });

      auto subs5 = receiver.subscribe("gamma",
                                      [&](const ComPacket::ConstSharedPacket& packet) {
                                        deserialise(packet, state.gamma);
                                        BOOST_LOG_TRIVIAL(trace) << "Gamma new value: " << state.gamma;
                                        stateUpdated = true;
                                    });

      auto subs6 = receiver.subscribe("fov",
                                      [&](const ComPacket::ConstSharedPacket& packet) {
                                        deserialise(packet, state.fov);
                                        // To radians:
                                        state.fov = state.fov * (M_PI / 180.f);
                                        BOOST_LOG_TRIVIAL(trace) << "FOV new value: " << state.fov;
                                        stateUpdated = true;
                                      });

      auto subs7 = receiver.subscribe("load_nif",
                                      [&](const ComPacket::ConstSharedPacket& packet) {
                                        deserialise(packet, state.newNif);
                                        BOOST_LOG_TRIVIAL(trace) << "Received new NIF path: " << state.newNif;
                                        stateUpdated = true;
                                      });

      auto subs8 = receiver.subscribe("interactive_samples",
                                      [&](const ComPacket::ConstSharedPacket& packet) {
                                        deserialise(packet, state.interactiveSamples);
                                        BOOST_LOG_TRIVIAL(trace) << "Interactive samples new value: " << state.interactiveSamples;
                                        stateUpdated = true;
                                      });

      BOOST_LOG_TRIVIAL(info) << "User interface server entering Tx/Rx loop.";
      serverReady = true;
      while (!stopServer && receiver.ok()) {
        std::this_thread::sleep_for(5ms);
      }
    }
    BOOST_LOG_TRIVIAL(info) << "User interface server Tx/Rx loop exited.";
  }

  /// Wait until server has initialised everything and enters its main loop:
  void waitUntilReady() {
    while (!serverReady) {
      std::this_thread::sleep_for(5ms);
    }
  }

public:
  enum class Status {
    Stop,
    Restart,
    Continue,
    Disconnected
  };

  struct State {
    float envRotationDegrees = 0.f;
    float exposure = 0.f;
    float gamma = 2.2f;
    float fov = 1.58f;
    std::uint32_t interactiveSamples = 1;
    std::string newNif;
    bool stop = false;
    bool detach = false;
  };

  InterfaceServer(int portNumber)
      : port(portNumber),
        stopServer(false),
        serverReady(false),
        stateUpdated(false) {}

  /// Return a copy of the state and mark it as consumed:
  State consumeState() {
    State tmp = state;
    stateUpdated = false;  // Clear the update flag.
    state.newNif.clear();  // Clear model load request.
    return tmp;
  }

  const State& getState() const {
    return state;
  }

  /// Has the state changed since it was last consumed?:
  bool stateChanged() const {
    return stateUpdated;
  }

  /// Launches the UI thread and blocks until a connection is
  /// made and all server state is initialised. Note that some
  /// server state can not be initialised until after the client
  /// has connected.
  void start() {
    stopServer = false;
    serverReady = false;
    stateUpdated = false;
    thread.reset(new std::thread(&InterfaceServer::communicate, this));
    waitUntilReady();
  }

  void initialiseVideoStream(std::size_t width, std::size_t height) {
    if (videoStream) {
      videoStream->AddVideoStream(width, height, 30, video::FourCc('F', 'M', 'P', '4'));
    } else {
      BOOST_LOG_TRIVIAL(warning) << "No object to add video stream to.";
    }
  }

  void stop() {
    stopServer = true;
    if (thread != nullptr) {
      try {
        thread->join();
        thread.reset();
        BOOST_LOG_TRIVIAL(trace) << "Server thread joined successfuly";
        sender.reset();
      } catch (std::system_error& e) {
        BOOST_LOG_TRIVIAL(error) << "User interface server thread could not be joined.";
      }
    }
  }

  void updateProgress(int step, int totalSteps) {
    if (sender) {
      serialise(*sender, "progress", step / (float)totalSteps);
    }
  }

  void updateSampleRate(float pathRate, float rayRate) {
    if (sender) {
      serialise(*sender, "sample_rate", SampleRates{pathRate, rayRate});
    }
  }

  void sendPreviewImage(const cv::Mat& ldrImage) {
    VideoFrame frame(ldrImage.data, AV_PIX_FMT_BGR24, ldrImage.cols, ldrImage.rows, ldrImage.step);
    bool ok = videoStream->PutVideoFrame(frame);
    if (!ok) {
      BOOST_LOG_TRIVIAL(warning) << "Could not send video frame.";
    }
  }

  virtual ~InterfaceServer() {
    stop();
  }

private:
  int port;
  TcpSocket serverSocket;
  std::unique_ptr<std::thread> thread;
  std::atomic<bool> stopServer;
  std::atomic<bool> serverReady;
  std::atomic<bool> stateUpdated;
  std::unique_ptr<TcpSocket> connection;
  std::unique_ptr<PacketMuxer> sender;
  std::unique_ptr<LibAvWriter> videoStream;
  State state;
  cv::Mat hdrImage;
};


int main(int argc, char* argv[]) {
    auto args = parseOptions(argc, argv, getOptions());

    // Set up logging level
    namespace logging = boost::log;
    auto logLevel = args.at("log-level").as<std::string>();
    std::stringstream ss(logLevel);
    logging::trivial::severity_level level;
    ss >> level;
    logging::core::get()->set_filter(logging::trivial::severity >= level);

    // Get port from command line arguments
    int port = args.at("port").as<int>();
    BOOST_LOG_TRIVIAL(info) << "Starting server on port " << port;

    // Create and start the server
    InterfaceServer server(port);
    server.start();

    // Create a simple test image to send periodically
    cv::Mat testImage(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
    server.initialiseVideoStream(testImage.cols, testImage.rows);

    // Main loop - keep running until interrupted
    try {
        int frameOffset = 0;
        while (!server.getState().stop) {

            // Update test image (create a scrolling gradient)
            for (int y = 0; y < testImage.rows; y++) {
                for (int x = 0; x < testImage.cols; x++) {
                    // Add frameOffset to x to create scrolling effect
                    int shiftedX = (x + frameOffset) % testImage.cols;
                    testImage.at<cv::Vec3b>(y, x) = cv::Vec3b(
                        shiftedX % 255,                // Blue
                        (shiftedX + y) % 255,          // Green
                        y % 255                        // Red
                    );
                }
            }

            // Increment the offset for the next frame
            frameOffset = (frameOffset + 1) % testImage.cols;

            // Send a preview image
            server.sendPreviewImage(testImage);

            // Send some fake progress updates
            static int step = 0;
            const int totalSteps = 100;
            step = (step + 1) % totalSteps;
            server.updateProgress(step, totalSteps);

            // Send fake sample rates
            server.updateSampleRate(1000.0f, 50000.0f);

            // Check if state was updated by client
            if (server.stateChanged()) {
                auto state = server.consumeState();
                BOOST_LOG_TRIVIAL(info) << "State updated:";
                BOOST_LOG_TRIVIAL(info) << "  Environment rotation: " << state.envRotationDegrees;
                BOOST_LOG_TRIVIAL(info) << "  Exposure: " << state.exposure;
                BOOST_LOG_TRIVIAL(info) << "  Gamma: " << state.gamma;
                BOOST_LOG_TRIVIAL(info) << "  FOV: " << state.fov;

                if (!state.newNif.empty()) {
                    BOOST_LOG_TRIVIAL(info) << "  New NIF requested: " << state.newNif;
                }

                if (state.detach) {
                    BOOST_LOG_TRIVIAL(info) << "Client requested detach. Exiting.";
                    break;
                }
            }

            // Sleep to avoid consuming too much CPU:
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Exception in main loop: " << e.what();
    }

    BOOST_LOG_TRIVIAL(info) << "Stopping server...";
    server.stop();
    BOOST_LOG_TRIVIAL(info) << "Server stopped. Exiting.";

    return 0;
}
