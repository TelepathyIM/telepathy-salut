SUBDIRS = tests

noinst_LTLIBRARIES = libgibber.la

BUILT_SOURCES = \
  gibber-file-transfer-enumtypes.c \
  gibber-file-transfer-enumtypes.h \
  gibber-signals-marshal.list \
  gibber-signals-marshal.h \
  gibber-signals-marshal.c

HANDWRITTEN_SOURCES =             \
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
  gibber-bytestream-direct.h      \
  gibber-bytestream-direct.c      \
  gibber-debug.c                  \
  gibber-debug.h                  \
  gibber-xmpp-reader.c            \
  gibber-xmpp-reader.h            \
  gibber-xmpp-writer.c            \
  gibber-xmpp-writer.h            \
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
  gibber-namespaces.h             \
  gibber-file-transfer.c          \
  gibber-file-transfer.h          \
  gibber-oob-file-transfer.c      \
  gibber-oob-file-transfer.h      \
  gibber-listener.c               \
  gibber-listener.h               \
  gibber-xmpp-error.h             \
  gibber-xmpp-error.c             \
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

gibber-signals-marshal.list: $(HANDWRITTEN_SOURCES) Makefile.am
	$(AM_V_GEN)( cd $(srcdir) && \
	sed -n -e 's/.*_gibber_signals_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(HANDWRITTEN_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
	else \
		mv $@.tmp $@; \
	fi

%-signals-marshal.h: %-signals-marshal.list Makefile.am
	$(AM_V_GEN)glib-genmarshal --header --prefix=_$(subst -,_,$*)_signals_marshal $< > $@

%-signals-marshal.c: %-signals-marshal.list Makefile.am
	$(AM_V_GEN){ echo '#include "$*-signals-marshal.h"' && \
	glib-genmarshal --body --prefix=_$(subst -,_,$*)_signals_marshal $< ; \
	} > $@


AM_CFLAGS = $(ERROR_CFLAGS) $(GCOV_CFLAGS) @GLIB_CFLAGS@ @LIBXML2_CFLAGS@ @WOCKY_CFLAGS@ @LIBSOUP_CFLAGS@

AM_LDFLAGS = $(GCOV_LIBS) @GLIB_LIBS@ @LIBXML2_LIBS@ @WOCKY_LIBS@ @LIBSOUP_LIBS@

# rules for making the glib enum objects
%-enumtypes.h: %.h Makefile.in
	$(AM_V_GEN)glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | LC_ALL=C tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | LC_ALL=C tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define $(shell echo $* | LC_ALL=C tr [:lower:]- [:upper:]_ | sed 's/_.*//')_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | LC_ALL=C tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
	$< > $@

%-enumtypes.c: %.h Makefile.in
	$(AM_V_GEN)glib-mkenums \
	--fhead "#include <$*.h>\n#include <$*-enumtypes.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@VALUENAME@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$< > $@
