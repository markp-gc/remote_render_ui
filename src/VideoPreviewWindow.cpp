// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include "VideoPreviewWindow.hpp"

#include <PacketComms.h>

#include <boost/log/trivial.hpp>

VideoPreviewWindow::VideoPreviewWindow(
  nanogui::Screen* screen, const std::string& title, PacketDemuxer& receiver)
  : nanogui::Window(screen, title),
    videoClient(std::make_unique<VideoClient>(receiver, "render_preview")),
    texture(nullptr),
    mbps(0.0),
    newFrameDecoded(false),
    runDecoderThread(true)
{
  using namespace nanogui;
  using namespace std::chrono_literals;
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
    this->set_size(Vector2i(w, h));
    this->set_layout(new GroupLayout(0));
    imageView = new ImageView(this);

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
    startDecodeThread();

  } else {
    BOOST_LOG_TRIVIAL(warning) << "Failed to initialise video stream.";
  }
}

VideoPreviewWindow::~VideoPreviewWindow() {
  stopDecodeThread();
}

void VideoPreviewWindow::startDecodeThread() {
  // Thread just decodes video frames as fast as it can:
  videoDecodeThread.reset(new std::thread([&]() {
    BOOST_LOG_TRIVIAL(debug) << "Video decode thread launched.";
    if (videoClient == nullptr) {
      BOOST_LOG_TRIVIAL(debug) << "Video client must be initialised before decoding.";
      throw std::logic_error("No VideoClient object available.");
    }

    while(runDecoderThread) { decodeVideoFrame(); }
  }));
}

void VideoPreviewWindow::stopDecodeThread() {
  runDecoderThread = false;
  if (videoDecodeThread) {
    try {
      videoDecodeThread->join();
      BOOST_LOG_TRIVIAL(debug) << "Video decode thread joined successfully.";
      videoDecodeThread.reset();
    } catch (std::system_error& e) {
      BOOST_LOG_TRIVIAL(warning) << "Video decode thread could not be joined.";
    }
  }
}

/// Decode a video frame into the buffer.
void VideoPreviewWindow::decodeVideoFrame() {
  newFrameDecoded = videoClient->receiveVideoFrame(
    [this](LibAvCapture& stream) {
      BOOST_LOG_TRIVIAL(debug) << "Decoded video frame";
      auto w = stream.GetFrameWidth();
      auto h = stream.GetFrameHeight();
      if (texture != nullptr) {
        // Extract decoded data to the buffer:
        std::lock_guard<std::mutex> lock(bufferMutex);
        if (texture->channels() == 3) {
          stream.ExtractRgbImage(bgrBuffer.data(), w * texture->channels());
        } else if (texture->channels() == 4) {
          stream.ExtractRgbaImage(bgrBuffer.data(), w * texture->channels());
        } else {
          throw std::runtime_error("Unsupported number of texture channels");
        }
      }
  });

  if (newFrameDecoded) {
    double bps = videoClient->computeVideoBandwidthConsumed();
    mbps = bps/(1024.0*1024.0);
    BOOST_LOG_TRIVIAL(debug) << "Video bit-rate: " << mbps << " Mbps" << std::endl;
  }
}

void VideoPreviewWindow::draw(NVGcontext* ctx) {
  // Upload latest buffer contents to video texture:
  if (texture != nullptr) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    texture->upload(bgrBuffer.data());
  }

  nanogui::Window::draw(ctx);
}
