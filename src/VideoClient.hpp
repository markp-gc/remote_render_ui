/*
    Copyright (C) Mark Pupilli 2013, All rights reserved
*/
#ifndef __VIDEO_CLIENT_H__
#define __VIDEO_CLIENT_H__

#include <VideoLib.h>

#include <PacketComms.h>

#include <cinttypes>
#include <memory>

// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include <boost/log/trivial.hpp>

class PacketDemuxer;

/**
    Class for managing a video stream received over the muxer comms system.

    In the constructor a subscription is made to AvData packets which are
    simply enqued as they are received. Note that the server must be sending
    packets with the same name.
*/
class VideoClient {
 public:
  VideoClient(PacketDemuxer& demuxer, const std::string& avPacketName);
  virtual ~VideoClient();

  bool initialiseVideoStream(const std::chrono::seconds& videoTimeout);
  int getFrameWidth() const { return m_streamer->GetFrameWidth(); };
  int getFrameHeight() const { return m_streamer->GetFrameHeight(); };

  bool receiveVideoFrame(std::function<void(LibAvCapture&)>);

  double computeVideoBandwidthConsumed();

 protected:
  bool streamerOk() const;
  bool streamerIoError() const;
  int readPacket(uint8_t* buffer, int size);

 private:
  SimpleQueue m_avInfoPackets;
  SimpleQueue m_avDataPackets;
  int m_packetOffset;
  uint64_t m_lastTotalVideoBytes;
  uint64_t m_totalVideoBytes;
  PacketSubscription m_avDataSubscription;

  std::unique_ptr<FFMpegStdFunctionIO> m_videoIO;
  std::unique_ptr<LibAvCapture> m_streamer;

  void resetAvTimeout();
  bool avHasTimedOut();
  std::chrono::steady_clock::time_point m_avDataTimeoutPoint;
  std::chrono::seconds m_avTimeout;
  std::chrono::steady_clock::time_point m_lastBandwidthCalcTime;
};

#endif /* __VIDEO_CLIENT_H__ */
