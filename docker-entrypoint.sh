#!/bin/bash
Xvfb :99 -screen 0 "${SCREEN_DIMENSIONS}" &
openttd "$@"
