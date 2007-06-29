SUBDIRS=examples

noinst_LTLIBRARIES = libgibber.la

BUILT_SOURCES = \
  gibber-bytestream-ibb-signals-marshal.c     \
  gibber-bytestream-ibb-signals-marshal.h     \
  gibber-transport-signals-marshal.c          \
  gibber-transport-signals-marshal.h          \
  gibber-muc-connection-signals-marshal.c     \
  gibber-muc-connection-signals-marshal.h     \
  gibber-xmpp-connection-signals-marshal.c    \
  gibber-xmpp-connection-signals-marshal.h    \
  gibber-xmpp-reader-signals-marshal.c        \
  gibber-xmpp-reader-signals-marshal.h        \
  gibber-xmpp-stanza-signals-marshal.c        \
  gibber-xmpp-stanza-signals-marshal.h        \
  gibber-sasl-auth-signals-marshal.c          \
  gibber-sasl-auth-signals-marshal.h          \
  gibber-r-multicast-sender-signals-marshal.c \
  gibber-r-multicast-sender-signals-marshal.h

HANDWRITTEN_SOURCES =             \
  gibber-xmpp-connection.c        \
  gibber-xmpp-connection.h        \
  gibber-xmpp-connection-listener.c \
  gibber-xmpp-connection-listener.h \
  gibber-muc-connection.c         \
  gibber-muc-connection.h         \
  gibber-bytestream-ibb.h         \
  gibber-bytestream-ibb.c         \
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
  libmd5-rfc/md5.c                \
  libmd5-rfc/md5.h

libgibber_la_SOURCES = $(HANDWRITTEN_SOURCES) $(BUILT_SOURCES)

if HAVE_LIBSSL
libgibber_la_SOURCES +=     \
    gibber-ssl-transport.c  \
    gibber-ssl-transport.h
endif


CLEANFILES=$(BUILT_SOURCES)
dist-hook:
	$(shell for x in $(BUILT_SOURCES); do rm -f $(distdir)/$$x ; done)

EXTRA_DIST = \
  gibber-bytestream-ibb-signals-marshal.list \
  gibber-r-multicast-sender-signals-marshal.list  \
  gibber-sasl-auth-signals-marshal.list           \
  gibber-ssl-transport-signals-marshal.list       \
  gibber-transport-signals-marshal.list           \
  gibber-muc-connection-signals-marshal.list      \
  gibber-xmpp-connection-signals-marshal.list     \
  gibber-xmpp-reader-signals-marshal.list         \
  gibber-xmpp-stanza-signals-marshal.list

signals-marshal.list: $(HANDWRITTEN_SOURCES) Makefile.am
	sed -n -e 's/.*_gibber_signals_marshal_\([A-Z]*__[A-Z_]*\).*/\1/p' \
		$(HANDWRITTEN_SOURCES) \
		| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@

signals-marshal.h: signals-marshal.list
	glib-genmarshal --header --prefix=_gibber_signals_marshal $< > $@

signals-marshal.c: signals-marshal.list
	glib-genmarshal --body --prefix=_gibber_signals_marshal $< > $@


AM_CFLAGS = $(ERROR_CFLAGS) @GLIB_CFLAGS@ @LIBXML2_CFLAGS@

if HAVE_LIBSSL
  AM_CFLAGS += @LIBSSL_CFLAGS@
endif

AM_LDFLAGS = @GLIB_LIBS@ @LIBXML2_LIBS@

if HAVE_LIBSSL
  AM_LDFLAGS += @LIBSSL_LIBS@
endif
