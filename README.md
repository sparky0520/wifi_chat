# LANChat

A basic Qt 6 peer-to-peer chat app for users on the same WiFi network. Discovery uses UDP broadcast, and messaging uses TCP.

## Requirements
- Qt 6 (Widgets + Network modules)
- CMake 3.16+
- C++17 compiler

## Build (Windows)
1. Open a terminal in the project folder.
2. Configure:
   cmake -S . -B build
3. Build:
   cmake --build build

## Run
- Start two instances on the same WiFi network.
- Enter a name and keep visibility enabled.
- Select a nearby user and send messages.

## Notes
- Chat history is stored in chat_history.txt next to the executable.
- If discovery fails, ensure UDP is not blocked by your firewall.
