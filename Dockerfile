FROM alpine:3.12
MAINTAINER  Jessica Stokes <hello@jessicastokes.net>

WORKDIR /tmp/workdir

ADD . /tmp/workdir

RUN apk add \
      --no-cache \
      xvfb \
      xdotool

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

WORKDIR /

RUN apk del .makedepends && \
    rm -rf /tmp/workdir

CMD ["openttd"]
