
FROM debian:11.6-slim as dev
ENV DEBIAN_FRONTEND noninteractive

ENV HOME /opt
RUN mkdir -p $HOME /build /opt

#required packages
RUN apt-get update -qq && \
    apt-get install -y build-essential wget git curl libsigc++-2.0-dev \
        libjansson-dev libcurl4-openssl-dev libluajit-5.1-dev libsqlite3-dev \
        libcurl4-openssl-dev libusb-dev libow-dev imagemagick libev-dev unzip \
        zip cmake automake autoconf libtool autopoint gettext libusb-1.0-0-dev \
        knxd knxd-dev googletest libuv1-dev libmosquitto-dev libmosquittopp-dev \
        libola-dev ola

# Clean up APT when done.
RUN apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* /build/*

ENV PKG_CONFIG_PATH="/opt/lib/pkgconfig"

FROM dev as builder

RUN git clone https://github.com/calaos/calaos_base.git && \
    cd calaos_base && ./autogen.sh && \
    ./configure --prefix=/opt CPPFLAGS=-I/opt/include LDFLAGS=-L/opt/lib && \
    make -j$(nproc) && \
    make install-strip

FROM debian:11.6-slim as runner

RUN apt -y update && \
    apt -y upgrade && \
    apt-get install -yq --no-install-recommends libuv1 wget libsigc++-2.0-0v5 libjansson4 \
        libluajit-5.1 libsqlite3-0 libusb-1.0 imagemagick libow-3.2 libev4 unzip zip knxd \
        libmosquitto1 libmosquittopp1 libowcapi-3.2 libcurl4 libprotobuf23 ola

# Clean up APT when done.
RUN apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* /build/*

COPY --from=builder /opt/ /opt/