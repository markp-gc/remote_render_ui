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

        BOOST_LOG_TRIVIAL(info) << "User interface server opening port " << port;
        bool ok = serverSocket.Bind(port);

        if (ok) {
            ok = serverSocket.Listen(0);
        }

        if (ok) {
            BOOST_LOG_TRIVIAL(info) << "User interface server accepting connections...";
            connection = serverSocket.Accept();
        }

        if (connection) {
            BOOST_LOG_TRIVIAL(debug) << "User interface client connected.";
            connection->setBlocking(false);
            PacketDemuxer receiver(*connection, packets::packetTypes);
            sender.reset(new PacketMuxer(*connection, packets::packetTypes));

            syncWithClient(*sender, receiver, "ready");
            BOOST_LOG_TRIVIAL(debug) << "Comms synchronised.";

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
            BOOST_LOG_TRIVIAL(debug) << "Video stream initialised.";

            auto subs1 = receiver.subscribe("stop",
                                            [&](const ComPacket::ConstSharedPacket& packet) {
                                                deserialise(packet, state.stop);
                                                BOOST_LOG_TRIVIAL(trace) << "Render stopped by remote UI.";
                                                stateUpdated = true;
                                            });

            auto subs2 = receiver.subscribe("value",
                                            [&](const ComPacket::ConstSharedPacket& packet) {
                                                deserialise(packet, state.value);
                                                BOOST_LOG_TRIVIAL(trace) << "New value: " << state.value;
                                                stateUpdated = true;
                                            });

            BOOST_LOG_TRIVIAL(info) << "User interface server entering Tx/Rx loop.";
            serverReady = true;
            while (serverReady && receiver.ok()) {
                std::this_thread::sleep_for(5ms);
            }
            BOOST_LOG_TRIVIAL(info) << "User interface server Tx/Rx loop exited.";
            // This needs to be freed while FFMpegStdFunctionIO is in scope in order
            // to write a trailer to the stream:
            videoStream.reset();
            serverReady = false;
        } else {
            BOOST_LOG_TRIVIAL(error) << "Failed to start user interface server.";
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
        State() : value(1.f), stop(false) {}
        std::string toString() const{
            return "State(value=" + std::to_string(value) + ", stop=" + std::to_string(stop) + ")";
        }

        float value;
        bool stop;
    };

    InterfaceServer(int portNumber)
        : port(portNumber),
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
        serverReady = false;
        stateUpdated = false;
        thread.reset(new std::thread(&InterfaceServer::communicate, this));
    }

    /// Wait until server has initialised everything and enters its main loop:
    /// @return true when the server is ready.
    bool waitUntilReady(std::uint32_t timeoutMs = 0) const {
        // If timeoutMs is 0, wait indefinitely
        if (timeoutMs == 0) {
            while (serverReady == false) {
                std::this_thread::sleep_for(5ms);
            }
            return true;
        }

        // Otherwise, use timeout logic
        int attempts = timeoutMs / 5; // Convert to a number of 5ms attempts

        for (int i = 0; i < attempts && serverReady == false; i++) {
            std::this_thread::sleep_for(5ms);
        }

        return connection != nullptr && serverReady;
    }

    void initialiseVideoStream(std::size_t width, std::size_t height) {
        if (videoStream) {
        videoStream->AddVideoStream(width, height, 30, video::FourCc('F', 'M', 'P', '4'));
        } else {
        BOOST_LOG_TRIVIAL(warning) << "No object to add video stream to.";
        }
    }

    void stop() {
        serverReady = false;
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
    std::atomic<bool> serverReady;
    std::atomic<bool> stateUpdated;
    std::unique_ptr<TcpSocket> connection;
    std::unique_ptr<PacketMuxer> sender;
    std::unique_ptr<LibAvWriter> videoStream;
    State state;
};
