// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include <vector>
#include <string>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

namespace packets {

const std::vector<std::string> packetTypes {
    "stop",                // Tell server to stop rendering and exit (client -> server)
    "steps",               // Update step count (client -> server)
    "value",               // Update server-side value (client -> server)
    "prompt",              // Update server-side prompt (client -> server)
    "playback_state",      // Update server-side playback state (client -> server)
    "render_preview",      // used to send compressed video packets (server->client)
                           // for render preview (server -> client)
    "ready",               // Used to sync with the other side once all other subscribers are ready (bi-directional)
};

// Struct and serialize function for HDR
// image data header packet.
struct HdrHeader {
  std::int32_t width;  // image width
  std::int32_t height; // image height
  // Data will be broken into this many chunks
  // for transmission so that the comms-link is not
  // blocking on a single giant image packets:
  std::uint32_t packets;
};

template <typename T>
void serialize(T& ar, HdrHeader& s) {
  ar(s.width, s.height, s.packets);
}

struct HdrPacket {
  std::uint32_t id;
  std::vector<float> data;
};

template <typename T>
void serialize(T& ar, HdrPacket& p) {
  ar(p.id, p.data);
}

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

} // end namespace packets
