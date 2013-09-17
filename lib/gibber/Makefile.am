SUBDIRS = tests

noinst_LTLIBRARIES = libgibber.la

HANDWRITTEN_SOURCES =             \
  gibber-muc-connection.c         \
  gibber-muc-connection.h         \
  gibber-bytestream-iface.h       \
  gibber-bytestream-iface.c       \
  gibber-bytestream-muc.h         \
  gibber-bytestream-muc.c         \
  gibber-bytestream-oob.h         \
  gibber-bytestream-oob.c         \
  gibber-bytestream-direct.h      \
  gibber-bytestream-direct.c      \
  gibber-debug.c                  \
  gibber-debug.h                  \
  gibber-transport.c              \
  gibber-transport.h              \
  gibber-fd-transport.c           \
  gibber-fd-transport.h           \
  gibber-tcp-transport.c          \
  gibber-tcp-transport.h          \
  gibber-unix-transport.c         \
  gibber-unix-transport.h         \
  gibber-multicast-transport.c    \
  gibber-multicast-transport.h    \
  gibber-r-multicast-transport.c  \
  gibber-r-multicast-transport.h  \
  gibber-r-multicast-causal-transport.c  \
  gibber-r-multicast-causal-transport.h  \
  gibber-r-multicast-transport.h  \
  gibber-r-multicast-packet.c     \
  gibber-r-multicast-packet.h     \
  gibber-r-multicast-sender.c     \
  gibber-r-multicast-sender.h     \
  gibber-linklocal-transport.c    \
  gibber-linklocal-transport.h    \
  gibber-file-transfer.c          \
  gibber-file-transfer.h          \
  gibber-oob-file-transfer.c      \
  gibber-oob-file-transfer.h      \
  gibber-listener.c               \
  gibber-listener.h               \
  gibber-sockets.c                \
  gibber-sockets.h                \
  gibber-sockets-unix.h           \
  gibber-sockets-win32.h          \
  gibber-util.h                   \
  gibber-util.c

libgibber_la_SOURCES = $(HANDWRITTEN_SOURCES) $(BUILT_SOURCES)

# Coding style checks
check_c_sources = \
    $(HANDWRITTEN_SOURCES)

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

CLEANFILES=$(BUILT_SOURCES)
dist-hook:
	$(shell for x in $(BUILT_SOURCES); do rm -f $(distdir)/$$x ; done)

AM_CFLAGS = $(ERROR_CFLAGS) $(GCOV_CFLAGS) @GLIB_CFLAGS@ @LIBXML2_CFLAGS@ @WOCKY_CFLAGS@ @LIBSOUP_CFLAGS@

AM_LDFLAGS = $(GCOV_LIBS) @GLIB_LIBS@ @LIBXML2_LIBS@ @WOCKY_LIBS@ @LIBSOUP_LIBS@

# Required for getnameinfo to work when cross compiling
if OS_WINDOWS
  AM_CFLAGS += -D_WIN32_WINNT=0x0501
endif

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer -:PROJECT telepathy-salut -:STATIC gibber-salut \
	 -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgibber_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(CFLAGS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	  $(AM_CFLAGS) \
	 -:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) \
	 -:LDFLAGS $(AM_LDFLAGS) \
	> $@
