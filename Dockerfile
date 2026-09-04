
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
        libsigc++-2.0-dev libcurl4-openssl-dev libluajit2-5.1-dev libsqlite3-dev \
        libcurl4-openssl-dev libow-dev imagemagick libev-dev libpugixml-dev \
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

# Python dependencies come from src/bin/calaos_mcp/pyproject.toml, and from
# nowhere else. That file is the one Dependabot watches; before T3.23 it was
# also the one no build path read, so the image installed whatever PyPI served
# that day. A rebuild on 2026-08-24 landed mcp 2.0.0, which no longer ships
# mcp.server.fastmcp, and the published sidecar could not even import itself.
#
# Copy the manifest (not the whole tree: this layer must only be invalidated
# when the dependencies change) and expand it into a requirements list.
COPY scripts/pyproject-requirements.py scripts/pydeps-conformance-probe.py \
     src/bin/calaos_mcp/pyproject.toml /tmp/calaos-pydeps/

# One single pip invocation for everything, on purpose. mcp, fastapi and
# starlette are coupled (fastapi 0.115.12 requires starlette>=0.40,<0.47), and
# roonapi/reolink-aio pull their own transitive constraints; installing them in
# separate passes lets a later pass silently upgrade a package an earlier one
# pinned. Resolving the whole set at once makes an incompatibility fail the
# build loudly instead of shipping a broken image.
#
# The probe then re-reads the .dist-info pip actually laid down: pip can exit
# 0 and still leave the set incomplete. Strict here -- an image that does not
# carry what it declares must not be published.
#
# roonapi and reolink-aio are deliberately not in pyproject.toml: they are
# optional integrations of calaos_server (extern procs), not sidecar runtime
# deps. They stay unpinned, as before, but now share the resolver pass.
RUN python3 /tmp/calaos-pydeps/pyproject-requirements.py \
        /tmp/calaos-pydeps/pyproject.toml > /tmp/calaos-pydeps/requirements.txt && \
    cat /tmp/calaos-pydeps/requirements.txt && \
    pip install --no-cache-dir --break-system-packages \
        -r /tmp/calaos-pydeps/requirements.txt roonapi reolink-aio && \
    CALAOS_PYDEPS_STRICT=1 \
        python3 /tmp/calaos-pydeps/pydeps-conformance-probe.py \
        /tmp/calaos-pydeps/pyproject.toml && \
    rm -rf /tmp/calaos-pydeps

ENV PKG_CONFIG_PATH="/opt/lib/pkgconfig"

FROM dev as builder

COPY . /calaos_base

RUN cd /calaos_base && ./autogen.sh && \
    sed -E -i 's/^#define[[:space:]]+PKG_VERSION_STR[[:space:]]+"[^"]+"/#define PKG_VERSION_STR "'"$APP_VERSION"'"/' src/bin/calaos_server/version.h && \
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
    apt-get install -yq --no-install-recommends libuv1 curl libsigc++-2.0-0v5 \
        libluajit2-5.1-dev libsqlite3-0 libusb-1.0 imagemagick libow-3.2 libev4 unzip zip knxd \
        libmosquitto1 libmosquittopp1 libowcapi-3.2 libcurl4 libpugixml1v5 ola python3 python3-pip python3-colorama openssl

# Python dependencies come from src/bin/calaos_mcp/pyproject.toml, and from
# nowhere else. That file is the one Dependabot watches; before T3.23 it was
# also the one no build path read, so the image installed whatever PyPI served
# that day. A rebuild on 2026-08-24 landed mcp 2.0.0, which no longer ships
# mcp.server.fastmcp, and the published sidecar could not even import itself.
#
# Copy the manifest (not the whole tree: this layer must only be invalidated
# when the dependencies change) and expand it into a requirements list.
COPY scripts/pyproject-requirements.py scripts/pydeps-conformance-probe.py \
     src/bin/calaos_mcp/pyproject.toml /tmp/calaos-pydeps/

# One single pip invocation for everything, on purpose. mcp, fastapi and
# starlette are coupled (fastapi 0.115.12 requires starlette>=0.40,<0.47), and
# roonapi/reolink-aio pull their own transitive constraints; installing them in
# separate passes lets a later pass silently upgrade a package an earlier one
# pinned. Resolving the whole set at once makes an incompatibility fail the
# build loudly instead of shipping a broken image.
#
# The probe then re-reads the .dist-info pip actually laid down: pip can exit
# 0 and still leave the set incomplete. Strict here -- an image that does not
# carry what it declares must not be published.
#
# roonapi and reolink-aio are deliberately not in pyproject.toml: they are
# optional integrations of calaos_server (extern procs), not sidecar runtime
# deps. They stay unpinned, as before, but now share the resolver pass.
RUN python3 /tmp/calaos-pydeps/pyproject-requirements.py \
        /tmp/calaos-pydeps/pyproject.toml > /tmp/calaos-pydeps/requirements.txt && \
    cat /tmp/calaos-pydeps/requirements.txt && \
    pip install --no-cache-dir --break-system-packages \
        -r /tmp/calaos-pydeps/requirements.txt roonapi reolink-aio && \
    CALAOS_PYDEPS_STRICT=1 \
        python3 /tmp/calaos-pydeps/pydeps-conformance-probe.py \
        /tmp/calaos-pydeps/pyproject.toml && \
    rm -rf /tmp/calaos-pydeps

# Clean up APT when done.
RUN apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/* /build/*

COPY --from=builder /opt/ /opt/
