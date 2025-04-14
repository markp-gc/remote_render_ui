// Copyright (c) 2025 Graphcore Ltd. All rights reserved.

#include <options.hpp>

#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>

#include <iostream>

#include "InterfaceServer.hpp"

boost::program_options::options_description getOptions() {
  namespace po = boost::program_options;
  po::options_description desc("Options");
  desc.add_options()
  ("help", "Show command help.")
  ("port", po::value<int>()->default_value(4242), "Port to listen for connections on.")
  ("log-level", po::value<std::string>()->default_value("debug"), "Set the log level to one of the following: 'trace', 'debug', 'info', 'warn', 'err', 'critical', 'off'.");
  return desc;
}

int main(int argc, char* argv[]) {
    auto args = parseOptions(argc, argv, getOptions());

    // Set up logging level
    auto logLevel = args.at("log-level").as<std::string>();
    InterfaceServer::setLogLevel(logLevel);

    // Get port from command line arguments
    int port = args.at("port").as<int>();
    BOOST_LOG_TRIVIAL(info) << "Starting server on port " << port;

    // Create and start the server
    InterfaceServer server(port);
    server.start();
    const bool ok = server.waitUntilReady();
    if (!ok) {
        BOOST_LOG_TRIVIAL(error) << "Failed to start server";
        return EXIT_FAILURE;
    }

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
            server.sendImage(testImage);

            // Send some fake progress updates
            static int step = 0;
            const int totalSteps = 100;
            step = (step + 1) % totalSteps;
            server.updateProgress(step, totalSteps);

            // Check if state was updated by client
            if (server.stateChanged()) {
                auto state = server.consumeState();
                BOOST_LOG_TRIVIAL(info) << "State updated:";
                BOOST_LOG_TRIVIAL(info) << state.toString();
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

    return EXIT_SUCCESS;
}
