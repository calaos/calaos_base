AM_CXXFLAGS = \
	$(CXX_PIC) \
	-I. \
	-I$(top_srcdir)/src/lib \
	-I$(top_srcdir)/src/lib/LuaScript \
	-I$(top_srcdir)/src/lib/http-parser \
	-I$(top_srcdir)/src/bin/calaos_server \
	-I$(top_srcdir)/src/bin/calaos_server/Rules \
	-I$(top_srcdir)/src/bin/calaos_server/IPCam \
	-I$(top_srcdir)/src/bin/calaos_server/Audio \
	-I$(top_srcdir)/src/bin/calaos_server/IO \
	-I$(top_srcdir)/src/bin/calaos_server/IO/libmbus \
	-DPACKAGE_BIN_DIR=\"$(bindir)\" \
	-DPACKAGE_LIB_DIR=\"$(libdir)\" \
	-DPACKAGE_DATA_DIR=\"$(pkgdatadir)/calaos\" \
	-DETC_DIR=\"$(sysconfdir)\" \
	@CALAOS_COMMON_CFLAGS@


noinst_LTLIBRARIES = \
	libcalaos_common.la

libcalaos_common_la_SOURCES = \
	base64.c 				\
	CalaosNetwork.cpp 			\
	Calendar.cpp 				\
	CThread.cpp 				\
	DownloadManager.cpp 			\
	EcoreTimer.cpp 				\
	EcoreFdHandler.cpp			\
	FileDownloader.cpp 			\
	Firmwares.cpp 				\
	IPC.cpp 				\
	Mutex.cpp 				\
	NTPClock.cpp 				\
	Params.cpp 				\
	SendMail.cpp 				\
	tcpsocket.cpp 				\
	threadManager.cpp 			\
	Utils.cpp 				\
	TinyXML/tinystr.cpp 			\
        TinyXML/tinyxml.cpp 			\
	TinyXML/tinyxmlerror.cpp 		\
        TinyXML/tinyxmlparser.cpp 		\
	LuaScript/ScriptBindings.cpp 		\
        LuaScript/ScriptManager.cpp 		\
        TimeRange.cpp 				\
	zibase.cpp				\
	Mutex.h					\
	CalaosNetwork.h				\
	LuaScript/ScriptManager.h		\
	LuaScript/ScriptBindings.h		\
	LuaScript/Lunar.h			\
	FakeLogging.h				\
	FileDownloader.h			\
	CThread.h				\
	NTPClock.h				\
	CalaosModule.h				\
	TinyXML/tinyxml.h			\
	TinyXML/tinystr.h			\
	Params.h				\
	EcoreTimer.h				\
	EcoreFdHandler.h			\
	SendMail.h				\
	Calendar.h				\
	base64.h				\
	Utils.h					\
	tcpsocket.h				\
	threadManager.h				\
	Jansson_Addition.h			\
	kashmir/randomstream.h			\
	kashmir/array.h				\
	kashmir/unique.h			\
	kashmir/iofwd.h				\
	kashmir/winrandom.h			\
	kashmir/iostate.h			\
	kashmir/devrandom.h			\
	kashmir/uuid.h				\
	kashmir/devrand.h			\
	kashmir/winrand.h			\
	Firmwares.h				\
	IPC.h					\
	DownloadManager.h			\
	http-parser/http_parser.h \
	http-parser/http_parser.c \
	zibase.h

libcalaos_common_la_LIBADD = -lm @CALAOS_COMMON_LIBS@

MAINTAINERCLEANFILES = Makefile.in
