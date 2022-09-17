// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include <vector>
#include <string>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

namespace packets {

const std::vector<std::string> packetTypes {
    "stop",            // Tell server to stop rendering and exit (client -> server)
    "detach",          // Detach the remote-ui but continue: server can destroy the
                       // communication interface and continue (client -> server)
    "progress",        // Send render progress (server -> client)
    "sample_rate",     // Send throughput measurement (server -> client)
    "env_rotation",    // Update environment light rotation (client -> server)
    "exposure",        // Update tone-map exposure (client -> server)
    "gamma",           // Update tone-map gamma (client -> server)
    "fov",             // Update field-of-view (client -> server)
    "load_nif",        // Insruct server to load a new
                       // NIF environemnt light (client -> server)
    "render_preview",  // used to send compressed video packets
                       // for render preview (server -> client)
    "hdr_header",      // Header for sending full uncompressed HDR
                       // image data (server -> client).
    "hdr_packet",      // Packet containing a portion of the full uncompressed
                       // HDR image (server -> client).
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
