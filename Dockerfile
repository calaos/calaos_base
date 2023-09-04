
FROM debian:12-slim as dev
ENV DEBIAN_FRONTEND noninteractive

ARG APP_VERSION
LABEL version=$APP_VERSION

ENV HOME /opt
RUN mkdir -p $HOME /build /opt

#required packages
RUN apt-get update -qq && \
    apt-get install -y build-essential wget git curl libsigc++-2.0-dev \
        libjansson-dev libcurl4-openssl-dev libluajit2-5.1-dev libsqlite3-dev \
        libcurl4-openssl-dev libusb-dev libow-dev imagemagick libev-dev unzip \
        zip cmake automake autoconf libtool autopoint gettext libusb-1.0-0-dev \
        knxd knxd-dev googletest libuv1-dev libmosquitto-dev libmosquittopp-dev \
        libola-dev ola tar gzip

RUN curl -L https://github.com/calaos/calaos-web-app/archive/refs/tags/3.0.1.tar.gz --output webapp.tar.gz && \
    tar xzvf webapp.tar.gz && \
    mkdir -p /opt/share/calaos/app && \
    mv calaos-web-app-*/dist/* /opt/share/calaos/app && \
    rm -fr webapp.tar.gz calaos-web-app-*

ENV PKG_CONFIG_PATH="/opt/lib/pkgconfig"

FROM dev as builder

RUN git clone https://github.com/calaos/calaos_base.git && \
    cd calaos_base && ./autogen.sh && \
    sed -i 's/^#define\s+PKG_VERSION_STR\s+"\w+"/#define PKG_VERSION_STR "'$APP_VERSION'"/g' src/bin/calaos_server/version.h && \
    echo $APP_VERSION > version && \
    ./configure --prefix=/opt CPPFLAGS=-I/opt/include LDFLAGS=-L/opt/lib && \
    make -j$(nproc) && \
    make install-strip

FROM debian:12-slim as runner

RUN apt -y update && \
    apt -y upgrade && \
    apt-get install -yq --no-install-recommends libuv1 curl libsigc++-2.0-0v5 libjansson4 \
        libluajit2-5.1-dev libsqlite3-0 libusb-1.0 imagemagick libow-3.2 libev4 unzip zip knxd \
        libmosquitto1 libmosquittopp1 libowcapi-3.2 libcurl4 ola

# Clean up APT when done.
RUN apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* /build/*

COPY --from=builder /opt/ /opt/