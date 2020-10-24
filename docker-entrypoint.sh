#!/bin/ash

# Start the headless X server
Xvfb "${DISPLAY}" \
  -screen "${X_SCREEN_NUMBER}" \
  "${X_SCREEN_DIMENSIONS}" &

loopCount=0
until xdpyinfo -display "${DISPLAY}" > /dev/null 2>&1
do
    loopCount=$((loopCount+1))
    sleep 1
    if [ ${loopCount} -gt 5 ]
    then
        echo "[ERROR] xvfb failed to start."
        exit 1
    fi
done

# Start the VNC server
x11vnc -display "${DISPLAY}" \
  -forever \
  -nopw &

wait $!

# Start OpenTTD
openttd "$@"
