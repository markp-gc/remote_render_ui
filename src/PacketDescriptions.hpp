// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include <vector>
#include <string>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

namespace packets {

const std::vector<std::string> packetTypes {
    "stop",                // Tell server to stop rendering and exit (client -> server)
    "progress",            // Send progress (server -> client)
    "value",               // Update server-side value (client -> server)
    "render_preview",      // used to send compressed video packets
                           // for render preview (server -> client)
    "ready",               // Used to sync with the other side once all other subscribers are ready (bi-directional)
};

} // end namespace packets
