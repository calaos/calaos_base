# Process this file with automake to create Makefile.in

ACLOCAL_AMFLAGS = -I m4



#lib_LIBRARIES      = libquickmail.a
#libquickmail_a_SOURCES = quickmail.c



lib_LTLIBRARIES     = libquickmail.la libquickmaillight.la

libquickmail_la_SOURCES = quickmail.c
libquickmail_la_CFLAGS = -O3 -DBUILD_QUICKMAIL_DLL
libquickmail_la_LDFLAGS = -lcurl $(WINSOCK)
libquickmail_la_HEADERS = quickmail.h
libquickmail_ladir = ${includedir}

libquickmaillight_la_SOURCES = quickmail.c smtpsocket.c
libquickmaillight_la_CFLAGS = -O3 -DBUILD_QUICKMAIL_DLL -DNOCURL
libquickmaillight_la_LDFLAGS = $(WINSOCK)
libquickmaillight_la_HEADERS = quickmail.h
libquickmaillight_ladir = ${includedir}



bin_PROGRAMS = quickmail quickmaillight

quickmail_SOURCES = quickmailprog.c
quickmail_LDADD = libquickmail.la
##quickmail_CFLAGS = -DBUILD_QUICKMAIL_STATIC
#quickmail_CFLAGS = -DSTATIC
#quickmail_LDFLAGS = -static -Wl,--as-needed -lcurl -lidn -lintl -liconv -lssh2 -lgcrypt -lgpg-error -lrtmp -lssl -lcrypto -lz -lgdi32 -lwldap32 -lws2_32 -lwinmm

quickmaillight_SOURCES = quickmailprog.c
quickmaillight_CFLAGS = -DNOCURL
quickmaillight_LDADD = libquickmaillight.la



check_PROGRAMS = test_quickmail test_quickmaillight

test_quickmail_SOURCES = test_quickmail.c
test_quickmail_LDADD = libquickmail.la
#test_quickmail_LDFLAGS = -static
#test_quickmail_LDFLAGS = -lquickmail

test_quickmaillight_SOURCES = test_quickmail.c
test_quickmaillight_LDADD = libquickmaillight.la
#test_quickmaillight_LDFLAGS = -static
#test_quickmaillight_LDFLAGS = -lquickmaillight

TESTS = test_quickmail test_quickmaillight



dist_man_MANS = man1/quickmail.1
