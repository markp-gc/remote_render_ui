# Remote User Interface

This is a prototype remote user-interface that connects to a rendering server running on a remote IPU machine.

It is built on top of:
- nanogui: a cross platform toolkit for simple GUI applications.
- packetcomms: a simple TCP/IP communication protocol designed for low latency.
- videolib: A wrapper for FFmpeg that supports TCP video streaming using packetcomms.

It has been tested on Mac OSX 11.6 and Ubuntu 18. It may also work on other platforms
where the dependencies can be satisfied.

# Building
```
git clone --recursive <URL-to-this-repo> remote-ui
mkdir remote-ui/build
cd remote-ui/build
cmake -G Ninja ..
ninja -j16
```

## Dependencies

The following are required dependencies:
- Cmake 3.20 or later (note that you can install the latest version via `pip install` if your system package manager installs an older version).
- Boost (only tested with 1.78).
- GLFW (Ubuntu) / Xcode with metal support (Mac OSX).
- FFmpeg (See README.md in videolib submodule for instructions and version compatibilty).

# Running

1. Launch the remote application on the remote host. Wait until it logs that it is waiting for connection.
  - E.g.: `[12:26:06.674469] [I] [51157] User interface server listening on port 4000`

2. Launch the client (this program) and connect to the same port:
  - E.g.: `./remote-ui --hostname <remote-hostname-or-IP-address> --port 4000 --nif-paths ../nifs.json`
  - The JSON file contains a list of paths to NIF models *on the remote*. These will be selectable in the UI.
  - Run with `--help` for a full list of options.
