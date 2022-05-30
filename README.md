# Remote User Interface

This is an early prototype of a remote user interface that connects to an application running on a remote IPU machine.
It is built on top of:
- nanogui: a cross platform toolkit for simple GUI applications.
- packetcomms: a simple TCP/IP communication protocol designed for low latency.
- videolib: A wrapper for FFMPEG that supports TCP video streaming using packetcomms.

It has been tested on Mac OSX 11.6 and Ubuntu 18. It may also work on other platforms
where the dependencies can be satisfied.

## Dependencies

A non-exhaustive list of dependencies is:
- Cmake 3.20 or later
- Boost
- GLFW (Ubuntu) / Xcode with metal support (Mac OSX)
