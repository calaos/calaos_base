
FROM debian:12-slim as dev
ENV DEBIAN_FRONTEND noninteractive

# Default locale of the image. Do not remove: a caller that does not forward a
# locale (podman exec / docker exec pass TERM but no LANG/LC_*) would leave the
# tools with no locale at all, and calaos_config then falls back to ASCII
# frames. C.UTF-8 is built into glibc and shipped by libc-bin, nothing has to be
# generated and the "locales" package is not needed.
ENV LANG=C.UTF-8

ARG APP_VERSION
LABEL version=$APP_VERSION

ENV HOME /opt
RUN mkdir -p $HOME /build /opt

#required packages
RUN apt-get update -qq && \
    apt-get install -y \
        build-essential wget git curl \
        libsigc++-2.0-dev libjansson-dev libcurl4-openssl-dev libluajit2-5.1-dev libsqlite3-dev \
        libcurl4-openssl-dev libow-dev imagemagick libev-dev \
        knxd knxd-dev googletest libuv1-dev libmosquitto-dev libmosquittopp-dev \
        libola-dev ola \
        unzip zip cmake automake autoconf libtool autopoint gettext  \
        tar gzip python3 python3-pip python3-colorama libssl-dev

# The web app is NOT embedded in the image. It ships as the calaos-web-app
# Debian package, and calaos-server.service bind-mounts it over this
# directory (-v /usr/share/calaos/webapp:/opt/share/calaos/app:ro).
# Standalone container users must provide their own mount on
# /opt/share/calaos/app; the empty directory only marks the mount point.
RUN mkdir -p /opt/share/calaos/app

RUN pip install roonapi --break-system-packages
RUN pip install reolink-aio --break-system-packages
RUN pip install "mcp[cli]" uvicorn fastapi websockets --break-system-packages

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

# Same reason as in the dev stage, and this is the image the tools ship in:
# calaos_config is started through "podman exec -it calaos-server
# /opt/bin/calaos_config", which sets TERM but forwards no LANG/LC_*. Without
# this variable the browser draws its frames in ASCII on a UTF-8 terminal.
ENV LANG=C.UTF-8

RUN apt -y update && \
    apt -y upgrade && \
    apt-get install -yq --no-install-recommends libuv1 curl libsigc++-2.0-0v5 libjansson4 \
        libluajit2-5.1-dev libsqlite3-0 libusb-1.0 imagemagick libow-3.2 libev4 unzip zip knxd \
        libmosquitto1 libmosquittopp1 libowcapi-3.2 libcurl4 ola python3 python3-pip python3-colorama openssl

RUN pip install roonapi --break-system-packages
RUN pip install reolink-aio --break-system-packages
RUN pip install "mcp[cli]" uvicorn fastapi websockets --break-system-packages

# Clean up APT when done.
RUN apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* /build/*

COPY --from=builder /opt/ /opt/
