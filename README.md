[Calaos Server](http://www.calaos.fr)
===============

[![License](https://img.shields.io/badge/license-GPLv3%2B-blue.svg)](http://www.gnu.org/licenses/gpl.html)

[![Build Status](https://travis-ci.org/calaos/calaos_base.svg?branch=master)](https://travis-ci.org/calaos/calaos_base)

Calaos is a powerful home automation software. It features a complete set of part to automate your house from lights to shutter or even music or cameras.

This is the base package for the server.

### Dependencies
Requires:

 - gcc > 5 or clang
 - libuv > 1.10
 - jansson > 2.5
 - curl > 7.20.0
 - luajit
 - sigc++ > 2.4.1
 - owfs (optional) [http://owfs.org/](http://owfs.org/)
 - libusb (optional)
 - imagemagick (optional)
 - OLA (optional) [https://github.com/OpenLightingProject/ola](https://github.com/OpenLightingProject/ola)
 - knxd (optional) [https://github.com/knxd/knxd](https://github.com/knxd/knxd)

### How to build

```
./autogen.sh
make
sudo make install
```

Contributing
------------

Want to contribute? Great! There are many ways to contribute, come get in touch with us on #calaos or #calaos.fr on freenode.
