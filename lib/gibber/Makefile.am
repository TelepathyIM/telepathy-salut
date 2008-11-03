SUBDIRS=examples

noinst_LTLIBRARIES = libgibber.la

BUILT_SOURCES = \
  signals-marshal.list \
  signals-marshal.h \
  signals-marshal.c

HANDWRITTEN_SOURCES =             \
  gibber-xmpp-connection.c        \
  gibber-xmpp-connection.h        \
  gibber-muc-connection.c         \
  gibber-muc-connection.h         \
  gibber-bytestream-ibb.h         \
  gibber-bytestream-ibb.c         \
  gibber-bytestream-iface.h       \
  gibber-bytestream-iface.c       \
  gibber-bytestream-muc.h         \
  gibber-bytestream-muc.c         \
  gibber-bytestream-oob.h         \
  gibber-bytestream-oob.c         \
  gibber-debug.c                  \
  gibber-debug.h                  \
  gibber-xmpp-node.c              \
  gibber-xmpp-node.h              \
  gibber-xmpp-reader.c            \
  gibber-xmpp-reader.h            \
  gibber-xmpp-stanza.c            \
  gibber-xmpp-stanza.h            \
  gibber-xmpp-writer.c            \
  gibber-xmpp-writer.h            \
  gibber-resolver.c               \
  gibber-resolver.h               \
  gibber-transport.c              \
  gibber-transport.h              \
  gibber-fd-transport.c           \
  gibber-fd-transport.h           \
  gibber-tcp-transport.c          \
  gibber-tcp-transport.h          \
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
  gibber-sasl-auth.c              \
  gibber-sasl-auth.h              \
  gibber-linklocal-transport.c    \
  gibber-linklocal-transport.h    \
  gibber-namespaces.h             \
  gibber-iq-helper.c              \
  gibber-iq-helper.h              \
  gibber-listener.c               \
  gibber-listener.h               \
  gibber-xmpp-connection-listener.c \
  gibber-xmpp-connection-listener.h \
  gibber-xmpp-error.h             \
  gibber-xmpp-error.c             \
  gibber-util.h                   \
  gibber-util.c

MD5_SOURCES =                     \
  libmd5-rfc/md5.c                \
  libmd5-rfc/md5.h

libgibber_la_SOURCES = $(HANDWRITTEN_SOURCES) $(BUILT_SOURCES) $(MD5_SOURCES)

if HAVE_LIBSSL
libgibber_la_SOURCES +=     \
    gibber-ssl-transport.c  \
    gibber-ssl-transport.h
endif

if HAVE_LIBASYNCNS
libgibber_la_SOURCES +=       \
  gibber-resolver-asyncns.c   \
  gibber-resolver-asyncns.h
endif

# Coding style checks
check_c_sources = \
    $(HANDWRITTEN_SOURCES)

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

CLEANFILES=$(BUILT_SOURCES)
dist-hook:
	$(shell for x in $(BUILT_SOURCES); do rm -f $(distdir)/$$x ; done)

signals-marshal.list: $(HANDWRITTEN_SOURCES) Makefile.am
	( cd $(srcdir) && \
	sed -n -e 's/.*_gibber_signals_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
		$(HANDWRITTEN_SOURCES) ) \
		| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@

signals-marshal.h: signals-marshal.list
	glib-genmarshal --header --prefix=_gibber_signals_marshal $< > $@

signals-marshal.c: signals-marshal.list
	glib-genmarshal --body --prefix=_gibber_signals_marshal $< > $@


AM_CFLAGS = $(ERROR_CFLAGS) $(GCOV_CFLAGS) @GLIB_CFLAGS@ @LIBXML2_CFLAGS@

if HAVE_LIBSSL
  AM_CFLAGS += @LIBSSL_CFLAGS@
endif

if HAVE_LIBASYNCNS
  AM_CFLAGS += @LIBASYNCNS_CFLAGS@
endif

AM_LDFLAGS = $(GCOV_LIBS) @GLIB_LIBS@ @LIBXML2_LIBS@ @RESOLV_LIBS@

if HAVE_LIBSSL
  AM_LDFLAGS += @LIBSSL_LIBS@
endif

if HAVE_LIBASYNCNS
  AM_LDFLAGS += @LIBASYNCNS_LIBS@
endif
