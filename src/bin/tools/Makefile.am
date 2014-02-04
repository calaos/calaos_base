
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
@CALAOS_HOME_CFLAGS@ \
-DLIBMBUS

bin_PROGRAMS = calaos_config wago_test calaos_thumb

calaos_config_SOURCES = \
        calaos_config.cpp

calaos_config_LDADD = \
	$(top_builddir)/src/lib/libcalaos_common.la \
	@CALAOS_COMMON_LIBS@

wago_test_SOURCES = \
	wago_test.cpp

wago_test_LDADD = \
	$(top_builddir)/src/lib/libcalaos_common.la \
	@CALAOS_COMMON_LIBS@

calaos_thumb_SOURCES = \
    main_cover.cpp

calaos_thumb_LDADD = \
    @CALAOS_HOME_LIBS@

