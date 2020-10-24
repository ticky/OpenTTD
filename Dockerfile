FROM alpine:3.12
MAINTAINER  Jessica Stokes <hello@jessicastokes.net>

WORKDIR /tmp/workdir

ADD . /tmp/workdir

RUN apk add \
      --no-cache \
      xvfb \
      x11vnc \
      xdpyinfo \
      libstdc++ \
      freetype \
      icu \
      libpng \
      sdl \
      zlib

RUN apk add \
      --no-cache \
      --virtual .makedepends \
      linux-headers \
      build-base \
      curl \
      freetype-dev \
      icu-dev \
      libpng-dev \
      sdl-dev \
      zlib-dev

# OpenTTD configuration borrowed from Alpine's own package https://git.alpinelinux.org/aports/tree/testing/openttd/APKBUILD
RUN ./configure \
      --prefix-dir=/usr \
      --binary-dir=bin \
      --with-sdl \
      --with-zlib \
      --with-freetype \
    make && \
    make install

WORKDIR /openttd

RUN apk del .makedepends && \
    rm -rf /tmp/workdir

ADD docker-entrypoint.sh /bin

ENV DISPLAY=":0"
ENV X_SCREEN_DIMENSIONS="640x480x24+32"
ENV VNC_PASSWORD="OpenTTD"

CMD ["docker-entrypoint.sh"]
