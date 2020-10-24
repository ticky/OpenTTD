#!/bin/bash

# Start the headless X server
Xvfb "${X_SERVER_NUMBER}" \
  -screen "${X_DISPLAY_NUMBER}" \
  "${X_SCREEN_DIMENSIONS}" &

# Start the VNC server
x11vnc -display "${X_SERVER_NUMBER}" \
  -forever \
  -nopw &

# Start OpenTTD
openttd "$@"
