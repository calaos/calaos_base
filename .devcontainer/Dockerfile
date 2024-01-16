FROM debian:12-slim as dev
ENV DEBIAN_FRONTEND noninteractive

ARG APP_VERSION
LABEL version=$APP_VERSION

ENV HOME /opt
RUN mkdir -p $HOME /build /opt

#required packages
RUN apt-get update -qq && \
    apt-get install -y \
        build-essential wget git curl \
        libsigc++-2.0-dev libjansson-dev libcurl4-openssl-dev libluajit2-5.1-dev libsqlite3-dev \
        libcurl4-openssl-dev libusb-dev libow-dev imagemagick libev-dev libusb-1.0-0-dev \
        knxd knxd-dev googletest libuv1-dev libmosquitto-dev libmosquittopp-dev \
        libola-dev ola \
        unzip zip cmake automake autoconf libtool autopoint gettext  \
        tar gzip zsh vim nano sudo zstd less gnupg ripgrep gdb cgdb

RUN echo "export EDITOR=nano \n\
    export VISUAL=nano \n\
    export LS_OPTIONS='--color=auto' \n\
    eval "$(dircolors)" \n\
    alias ls='ls $LS_OPTIONS' \n\
    alias ll='ls $LS_OPTIONS -l' \n\
    " >> /etc/bash.bashrc

RUN curl -fsSL https://starship.rs/install.sh | sh -s -- -y
RUN echo 'eval "$(starship init bash)"' >> /etc/bash.bashrc

#better bash history search with up/down arrows
RUN echo "\n\
## arrow up \n\
\"\e[A\":history-search-backward \n\
## arrow down \n\
\"\e[B\":history-search-forward \n\
\n\
set show-all-if-ambiguous on \n\
set completion-ignore-case on \n\
" >> /etc/inputrc
