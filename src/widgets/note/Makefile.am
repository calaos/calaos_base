MAINTAINERCLEANFILES = Makefile.in

AM_CXXFLAGS = \
-I. \
-I./Rules \
-I./IPCam \
-I./Audio \
-I./IO \
-I./IO/libmbus \
-I./Scenario \
-DPACKAGE_BIN_DIR=\"$(bindir)\" \
-DPACKAGE_LIB_DIR=\"$(libdir)\" \
-DPACKAGE_DATA_DIR=\"$(datadir)/calaos\" \
-I$(top_srcdir)/src/lib \
-I$(top_srcdir)/src/lib/LuaScript \
-I$(top_srcdir)/src/bin/calaos_home/controllers \
-I$(top_srcdir)/src/bin/calaos_home/views \
-I$(top_srcdir)/src/bin/calaos_home/views/GenlistItems \
-I$(top_srcdir)/src/bin/calaos_home/views/GengridItems \
-I$(top_srcdir)/src/bin/calaos_home/views/IO \
-I$(top_srcdir)/src/bin/calaos_home/models \
-I$(top_srcdir)/src/bin/calaos_home \
@CALAOS_COMMON_CFLAGS@ \
@CALAOS_HOME_CFLAGS@

controllerdir = $(libdir)/calaos/widgets/note
controller_LTLIBRARIES = module.la

module_la_SOURCES = \
	module.cpp \
	module.h


module_la_CFLAGS = \
	@CALAOS_COMMON_CFLAGS@ \
	@CALAOS_HOME_CFLAGS@
module_la_LIBADD = \
	@CALAOS_HOME_LIBS@ \
	@CALAOS_COMMON_LIBS@ \
	$(top_builddir)/src/lib/libcalaos_common.la

module_la_LDFLAGS = @CALAOS_LTMODULE_FLAGS@
module_la_LIBTOOLFLAGS = --tag=disable-static
