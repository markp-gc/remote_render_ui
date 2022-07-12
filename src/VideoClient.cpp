// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#include "VideoClient.hpp"
#include <PacketSerialisation.h>

#include <chrono>

#include <boost/log/trivial.hpp>

VideoClient::VideoClient(PacketDemuxer& demuxer, const std::string& avPacketName)
    : m_packetOffset(0),
      m_avDataSubscription(
          demuxer.subscribe(avPacketName, [this](const ComPacket::ConstSharedPacket& packet) {
            m_avDataPackets.emplace(packet);
            m_totalVideoBytes += packet->getDataSize();
            BOOST_LOG_TRIVIAL(trace) << "Received compressed video packet of size " << packet->getDataSize() << std::endl;
          })),
      m_avTimeout(0) {
}

VideoClient::~VideoClient() {
}

/**
    @param videoTimeout If no video data is received for longer than this duration then streaming will terminate.
*/
bool VideoClient::initialiseVideoStream(const std::chrono::seconds& videoTimeout) {
  m_avTimeout = videoTimeout;
  resetAvTimeout();

  m_lastBandwidthCalcTime = std::chrono::steady_clock::now();

  // Create a video reader object that uses function/callback IO:
  m_videoIO.reset(new FFMpegStdFunctionIO(FFMpegCustomIO::ReadBuffer,
                                          std::bind(&VideoClient::readPacket, std::ref(*this), std::placeholders::_1, std::placeholders::_2)));
  m_streamer.reset(new LibAvCapture(*m_videoIO));
  if (m_streamer->IsOpen() == false) {
    BOOST_LOG_TRIVIAL(debug) << "Failed to open video stream.";
    return false;
  }

  // Get some frames so we can extract correct image dimensions:
  bool gotFrame = false;
  for (int i = 0; i < 2; ++i) {
    gotFrame = m_streamer->GetFrame();
    m_streamer->DoneFrame();
  }

  if (gotFrame == false) {
    BOOST_LOG_TRIVIAL(debug) << "Failed to acquire frames from video stream.";
    return false;
  }

  auto w = getFrameWidth();
  auto h = getFrameHeight();
  BOOST_LOG_TRIVIAL(debug) << "Successfully initialised video stream: " << w << "x" << h;

  return true;
}

bool VideoClient::receiveVideoFrame(std::function<void(LibAvCapture&)> callback) {
  if (m_streamer == nullptr) {
    throw std::logic_error(std::string(__FUNCTION__) + ": streamer object not allocated.");
  }

  bool gotFrame = m_streamer->GetFrame();
  if (gotFrame) {
    callback(*m_streamer);
    m_streamer->DoneFrame();
  }

  return gotFrame;
}

/**
    @param seconds Time elapsed since last call to this function (assumes video has benn constantly streaming for this whole time).
    @return Bandwidth used by video stream in bits per second.
*/
double VideoClient::computeVideoBandwidthConsumed() {
  std::chrono::steady_clock::time_point timeNow = std::chrono::steady_clock::now();
  const auto t2 = std::chrono::steady_clock::now();
  double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(timeNow - m_lastBandwidthCalcTime).count() / 1000.0;
  double bits_per_sec = (m_totalVideoBytes - m_lastTotalVideoBytes) * (8.0 / seconds);
  m_lastTotalVideoBytes = m_totalVideoBytes;
  m_lastBandwidthCalcTime = timeNow;
  return bits_per_sec;
}

bool VideoClient::streamerOk() const {
  return m_streamer != nullptr && m_streamer->IoError() == false;
}

bool VideoClient::streamerIoError() const {
  if (m_streamer.get() == nullptr) {
    BOOST_LOG_TRIVIAL(error) << "Stream capture object not allocated." << std::endl;
    return false;  // Streamer not allocated yet (obviosuly this does not count as IO error)
  }

  return m_streamer->IoError();
}

/**
    This is called back by the LibAvCapture object when it wants to decode
    the next AV packet (i.e. in consequence of calling m_streamer->GetFrame()).
*/
int VideoClient::readPacket(uint8_t* buffer, int size) {
  using namespace std::chrono_literals;
  SimpleQueue::LockedQueue lockedQueue = m_avDataPackets.lock();
  while (m_avDataPackets.empty() && m_avDataSubscription.getDemuxer().ok()) {
    lockedQueue.waitNotEmpty(1s);

    if (m_avDataPackets.empty()) {
      if (avHasTimedOut()) {
        BOOST_LOG_TRIVIAL(error) << "VideoClient timed out waiting for an AV packet." << std::endl;
        return -1;
      }
    }
  }

  resetAvTimeout();

  // We were asked for more than packet contains so loop through packets until
  // we have returned what we needed or there are no more packets:
  int required = size;
  while (required > 0 && m_avDataPackets.empty() == false) {
    const ComPacket::ConstSharedPacket packet = m_avDataPackets.front();
    const int availableSize = packet->getData().size() - m_packetOffset;

    if (availableSize <= required) {
      // Current packet contains less than required so copy the whole packet
      // and continue:
      std::copy(packet->getData().begin() + m_packetOffset, packet->getData().end(), buffer);
      m_packetOffset = 0;  // Reset the packet offset so the next packet will be read from beginning.
      m_avDataPackets.pop();
      buffer += availableSize;
      required -= availableSize;
    } else {
      // Current packet contains more than enough to fulfill the request
      // so copy what is required and save the rest for later:
      auto startItr = packet->getData().begin() + m_packetOffset;
      std::copy(startItr, startItr + required, buffer);
      m_packetOffset += required;  // Increment the packet offset by the amount read from this packet.
      required = 0;
    }
  }

  return size - required;
}

void VideoClient::resetAvTimeout() {
  m_avDataTimeoutPoint = std::chrono::steady_clock::now() + m_avTimeout;
}

bool VideoClient::avHasTimedOut() {
  return std::chrono::steady_clock::now() > m_avDataTimeoutPoint;
}
