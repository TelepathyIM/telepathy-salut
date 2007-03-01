SUBDIRS=examples

noinst_LTLIBRARIES = libgibber.la

BUILT_SOURCES = \
  gibber-transport-signals-marshal.c          \
  gibber-transport-signals-marshal.h          \
  gibber-xmpp-connection-signals-marshal.c    \
  gibber-xmpp-connection-signals-marshal.h    \
  gibber-xmpp-reader-signals-marshal.c        \
  gibber-xmpp-reader-signals-marshal.h        \
  gibber-xmpp-stanza-signals-marshal.c        \
  gibber-xmpp-stanza-signals-marshal.h        \
  gibber-sasl-auth-signals-marshal.c          \
  gibber-sasl-auth-signals-marshal.h          \
  gibber-ssl-transport-signals-marshal.c      \
  gibber-ssl-transport-signals-marshal.h    

libgibber_la_SOURCES =            \
  gibber-xmpp-connection.c        \
  gibber-xmpp-connection.h        \
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
  gibber-ssl-transport.c          \
  gibber-ssl-transport.h          \
  gibber-sasl-auth.c              \
  gibber-sasl-auth.h              \
  gibber-linklocal-transport.c    \
  gibber-linklocal-transport.h    \
  gibber-namespaces.h             \
  libmd5-rfc/md5.c                \
  libmd5-rfc/md5.h                \
  $(BUILT_SOURCES)


CLEANFILES=$(BUILT_SOURCES)
dist-hook:
	$(shell for x in $(BUILT_SOURCES); do rm -f $(distdir)/$$x ; done)


%-signals-marshal.h: %-signals-marshal.list
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.h

%-signals-marshal.c: %-signals-marshal.list
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.c


AM_CFLAGS = $(ERROR_CFLAGS) @GLIB_CFLAGS@ @LIBXML2_CFLAGS@ @LIBSSL_CFLAGS@
AM_LDFLAGS = @GLIB_LIBS@ @LIBXML2_LIBS@ @LIBSSL_LIBS@
