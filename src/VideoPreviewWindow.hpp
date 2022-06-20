// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include <nanogui/nanogui.h>

#include "VideoClient.hpp"

/// Window that receives an encoded video stream and displays
/// it in a nanogui::ImageView that allows panning and zooming
/// of the image. Video is decoded in a separate thread to keep
/// the UI widgets responsive (although their effect will be
/// limited by the video rate).
class VideoPreviewWindow : public nanogui::Window {
public:
  VideoPreviewWindow(nanogui::Screen* screen, const std::string& title, PacketDemuxer& receiver);

  virtual ~VideoPreviewWindow();

  virtual void draw(NVGcontext* ctx);

  double getVideoBandwidthMbps() { return mbps; }

  void reset() { imageView->reset(); }

protected:
  void startDecodeThread();

  void stopDecodeThread();

  /// Decode a video frame into the buffer.
  void decodeVideoFrame();

private:
  std::unique_ptr<VideoClient> videoClient;
  std::vector<std::uint8_t> bgrBuffer;
  nanogui::Texture* texture;
  nanogui::ImageView* imageView;
  double mbps;

  std::unique_ptr<std::thread> videoDecodeThread;
  std::mutex bufferMutex;
  std::atomic<bool> newFrameDecoded;
  std::atomic<bool> runDecoderThread;
};
