// Copyright (c) 2025 Graphcore Ltd. All rights reserved.

#pragma once

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

#include <atomic>
#include <chrono>
#include <thread>

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
                BOOST_LOG_TRIVIAL(trace) << "Sender return status: " << sender->ok();
                return sender->ok() ? size : -1;
            }
            return -1;
        });
        videoStream.reset(new LibAvWriter(videoIO));

        auto subs1 = receiver.subscribe("steps",
                                        [&](const ComPacket::ConstSharedPacket& packet) {
                                            deserialise(packet, state.steps);
                                            BOOST_LOG_TRIVIAL(trace) << "New steps value: " << state.steps;
                                            stateUpdated = true;
                                        });

        auto subs2 = receiver.subscribe("stop",
                                        [&](const ComPacket::ConstSharedPacket& packet) {
                                            deserialise(packet, state.stop);
                                            BOOST_LOG_TRIVIAL(trace) << "Render stopped by remote UI.";
                                            stateUpdated = true;
                                        });

        auto subs3 = receiver.subscribe("value",
                                        [&](const ComPacket::ConstSharedPacket& packet) {
                                            deserialise(packet, state.value);
                                            BOOST_LOG_TRIVIAL(trace) << "New value: " << state.value;
                                            stateUpdated = true;
                                        });

        auto subs4 = receiver.subscribe("prompt",
                                        [&](const ComPacket::ConstSharedPacket& packet) {
                                            deserialise(packet, state.prompt);
                                            BOOST_LOG_TRIVIAL(trace) << "New prompt: " << state.prompt;
                                            stateUpdated = true;
                                        });

        auto subs5 = receiver.subscribe("playback_state",
                                        [&](const ComPacket::ConstSharedPacket& packet) {
                                            deserialise(packet, state.isPlaying);
                                            BOOST_LOG_TRIVIAL(trace) << "Playback state changed: "
                                                << (state.isPlaying ? "playing" : "paused");
                                            stateUpdated = true;
                                        });

        BOOST_LOG_TRIVIAL(info) << "User interface server entering Tx/Rx loop.";
        serverReady = true;
        while (!stopServer && receiver.ok()) {
            std::this_thread::sleep_for(5ms);
        }
        videoStream.reset(); // This needs to be freed while FFMpegStdFunctionIO is in scope so it can write a tariler.
        }
        serverReady = false;
        BOOST_LOG_TRIVIAL(info) << "User interface server Tx/Rx loop exited.";
    }

    /// Wait until server has initialised everything and enters its main loop:
    void waitUntilReady() const {
        while (!serverReady) {
            std::this_thread::sleep_for(5ms);
    }
}

public:

    static void setLogLevel(const std::string& string) {
        namespace logging = boost::log;
        std::stringstream ss(string);
        logging::trivial::severity_level level;
        ss >> level;
        logging::core::get()->set_filter(logging::trivial::severity >= level);
    }

    struct State {
        State() : prompt(""), steps(2), value(1.f), stop(false), isPlaying(true) {}
        std::string toString() const{
            return "State(prompt=" + prompt + ", steps=" + std::to_string(steps) + ", value=" + std::to_string(value) + ", stop=" + std::to_string(stop) +
                   ", isPlaying=" + std::to_string(isPlaying) + ")";
        }

        std::string prompt;
        std::uint32_t steps;
        float value;
        bool stop;
        bool isPlaying;
    };

    InterfaceServer(int portNumber)
        : port(portNumber),
        stopServer(false),
        serverReady(false),
        stateUpdated(false) {}

    std::string toString() const {
        return "InterfaceServer(port=" + std::to_string(port) + ", ready=" + std::to_string(serverReady) + ")";
    }

    /// Return a copy of the state and mark it as consumed:
    State consumeState() {
        State tmp = state;
        stateUpdated = false;  // Clear the update flag.
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

    void sendImage(const cv::Mat& ldrImage) {
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
};
