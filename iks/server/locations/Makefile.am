# No subdirectories
SUBDIRS = 

# Additional files for deleting on 'make maintainer-clean'
MAINTAINERCLEANFILES = Makefile.in

# Compilations flags
AM_CPPFLAGS = \
	-DPREFIX=\"@prefix@\" \
	-I@srcdir@/../ \
	-I@srcdir@/../../../libSzarp/include \
	-I@srcdir@/../../../libSzarp2/include \
	@XML_CFLAGS@ @XSLT_CFLAGS@ @LUA_CFLAGS@

LIBSZARP=../../../libSzarp/libSzarp.la
LIBSZARP2=../../../libSzarp2/libSzarp2.la

LDADD = @PTHREAD_CFLAGS@ $(LIBSZARP2) $(LIBSZARP) \
	@XSLT_LIBS@ @XML_LIBS@ @LUA_LIBS@ \
	@BOOST_LDFLAGS@ @BOOST_ASIO_LIB@ @BOOST_SYSTEM_LIB@ \
	@BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @BOOST_DATE_TIME_LIB@ \
	@BOOST_PROGRAM_OPTIONS_LIB@ @BOOST_LOCALE_LIB@

lib_LIBRARIES = libiks-server-locations.a

libiks_server_locations_a_SOURCES = \
	remotes_updater/remotes_updater.cpp \
	welcome/welcome.cpp \
	szbase/szbase.cpp \
	proxy/proxy.cpp \
	location.cpp \
	protocol_location.cpp \
	manager.cpp

