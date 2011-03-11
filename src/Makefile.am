BUILT_SOURCES = \
    salut-signals-marshal.list \
    salut-signals-marshal.h \
    salut-signals-marshal.c \
    salut-presence-enumtypes.h \
    salut-presence-enumtypes.c

# correctly clean the generated source files
CLEANFILES = $(BUILT_SOURCES)

dist-hook:
	$(shell for x in $(BUILT_SOURCES); do rm -f $(distdir)/$$x ; done)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = telepathy-salut.pc

libexec_PROGRAMS=telepathy-salut
noinst_PROGRAMS = write-mgr-file

CORE_SOURCES =                                          \
    salut-capabilities.c                                \
    salut-capabilities.h                                \
    salut-caps-channel-manager.c                        \
    salut-caps-channel-manager.h                        \
    salut-caps-hash.c                                   \
    salut-caps-hash.h                                   \
    salut-connection-manager.c                          \
    salut-connection-manager.h                          \
    salut-contact-manager.c                             \
    salut-contact-manager.h                             \
    salut-disco.c                                       \
    salut-disco.h                                       \
    salut-im-manager.c                                  \
    salut-im-manager.h                                  \
    salut-im-channel.c                                  \
    salut-im-channel.h                                  \
    salut-muc-manager.c                                 \
    salut-muc-manager.h                                 \
    salut-roomlist-manager.c                            \
    salut-roomlist-manager.h                            \
    salut-muc-channel.c                                 \
    salut-ft-manager.c                                  \
    salut-ft-manager.h                                  \
    salut-file-transfer-channel.c                       \
    salut-file-transfer-channel.h                       \
    salut-muc-channel.h                                 \
    salut-presence-cache.c                              \
    salut-presence-cache.h                              \
    salut-tubes-manager.c                               \
    salut-tubes-manager.h                               \
    salut-contact.h                                     \
    salut-contact.c                                     \
    salut-self.h                                        \
    salut-self.c                                        \
    salut-contact-channel.c                             \
    salut-contact-channel.h                             \
    salut-connection.c                                  \
    salut-connection.h                                  \
    salut-presence.h                                    \
    salut-contact-channel.h                             \
    salut-contact-channel.c                             \
    salut-si-bytestream-manager.h                       \
    salut-si-bytestream-manager.c                       \
    text-helper.c                                       \
    text-helper.h                                       \
    salut-roomlist-channel.h                            \
    salut-roomlist-channel.c                            \
    salut-xmpp-connection-manager.h                     \
    salut-xmpp-connection-manager.c                     \
    salut-discovery-client.h                            \
    salut-discovery-client.c                            \
    tube-dbus.h                                         \
    tube-dbus.c                                         \
    tube-iface.h                                        \
    tube-iface.c                                        \
    tube-stream.h                                       \
    tube-stream.c                                       \
    salut-tubes-channel.h                               \
    salut-tubes-channel.c                               \
    salut-util.h                                        \
    salut-util.c                                        \
    debug.c                                             \
    debug.h                                             \
    protocol.c                                          \
    protocol.h

AVAHI_BACKEND_SOURCES = 				\
    salut-avahi-discovery-client.h                      \
    salut-avahi-discovery-client.c                      \
    salut-avahi-muc-manager.h                           \
    salut-avahi-muc-manager.c                           \
    salut-avahi-roomlist-manager.h                      \
    salut-avahi-roomlist-manager.c                      \
    salut-avahi-muc-channel.h                           \
    salut-avahi-muc-channel.c                           \
    salut-avahi-contact-manager.h                       \
    salut-avahi-contact-manager.c                       \
    salut-avahi-contact.h                               \
    salut-avahi-contact.c                               \
    salut-avahi-self.h                                  \
    salut-avahi-self.c

if ENABLE_OLPC
  CORE_SOURCES +=                                       \
    salut-olpc-activity.h                               \
    salut-olpc-activity.c                               \
    salut-olpc-activity-manager.h                       \
    salut-olpc-activity-manager.c

  AVAHI_BACKEND_SOURCES +=                              \
    salut-avahi-olpc-activity-manager.h                 \
    salut-avahi-olpc-activity-manager.c                 \
    salut-avahi-olpc-activity.h                         \
    salut-avahi-olpc-activity.c
endif

DUMMY_BACKEND_SOURCES = 				\
    salut-dummy-discovery-client.h                      \
    salut-dummy-discovery-client.c

SHA1_SOURCES =                                          \
    sha1/sha1-util.h                                    \
    sha1/sha1-util.c

libsalut_convenience_la_SOURCES = $(CORE_SOURCES) $(BUILT_SOURCES) \
    $(SHA1_SOURCES)

libsalut_backend_avahi_la_SOURCES = $(AVAHI_BACKEND_SOURCES)

libsalut_backend_dummy_la_SOURCES = $(DUMMY_BACKEND_SOURCES)

write_mgr_file_SOURCES = write-mgr-file.c
write_mgr_file_LDADD = libsalut-convenience.la \
		       libsalut-backend-avahi.la \
                       $(top_builddir)/lib/gibber/libgibber.la \
                       $(top_builddir)/extensions/libsalut-extensions.la \
                       -ltelepathy-glib
telepathy_salut_SOURCES = \
    salut.c

# Coding style checks
check_c_sources = \
    $(telepathy_salut_SOURCES) \
    $(CORE_SOURCES) \
    $(AVAHI_BACKEND_SOURCES) \
    $(DUMMY_BACKEND_SOURCES) \
    $(write_mgr_file_SOURCES)

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

# TELEPATHY_GLIB_LIBS is only here so compiling against a static lib succeeds
telepathy_salut_LDADD = libsalut-convenience.la \
			libsalut-backend-avahi.la \
                        $(top_builddir)/lib/gibber/libgibber.la \
                        $(top_builddir)/extensions/libsalut-extensions.la \
                        -ltelepathy-glib

noinst_LTLIBRARIES = libsalut-convenience.la \
		     libsalut-backend-avahi.la \
		     libsalut-backend-dummy.la

AM_CFLAGS = \
	-I $(top_srcdir) -I $(top_builddir) \
	-I $(top_srcdir)/lib -I $(top_builddir)/lib \
	-DG_LOG_DOMAIN=\"salut\" \
	$(ERROR_CFLAGS) \
	$(GCOV_CFLAGS) \
	@LIBXML2_CFLAGS@ \
	@GLIB_CFLAGS@ \
	@WOCKY_CFLAGS@ \
	@DBUS_CFLAGS@ \
	@AVAHI_CFLAGS@ \
	@TELEPATHY_GLIB_CFLAGS@ \
	$(NULL)

AM_LDFLAGS = \
	$(GCOV_LIBS) \
	@LIBXML2_LIBS@ \
	@GLIB_LIBS@ \
	@WOCKY_LIBS@ \
	@DBUS_LIBS@ \
	@AVAHI_LIBS@ \
	@TELEPATHY_GLIB_LIBS@ \
	$(NULL)

# Teach it how to make libgibber.la
$(top_builddir)/lib/gibber/libgibber.la:
	${MAKE} -C $(top_builddir)/lib/gibber libgibber.la

.PHONY: $(top_builddir)/lib/gibber/libgibber.la

salut-signals-marshal.list: $(CORE_SOURCES) $(AVAHI_BACKEND_SOURCES) $(DUMMY_BACKEND_SOURCES) Makefile.am
	$(AM_V_GEN)( cd $(srcdir) && \
	sed -n -e 's/.*salut_signals_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(CORE_SOURCES) $(AVAHI_BACKEND_SOURCES) $(DUMMY_BACKEND_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
	else \
		mv $@.tmp $@; \
	fi

%-signals-marshal.h: %-signals-marshal.list Makefile.am
	$(AM_V_GEN)glib-genmarshal --header --prefix=$(subst -,_,$*)_signals_marshal $< > $@

%-signals-marshal.c: %-signals-marshal.list Makefile.am
	$(AM_V_GEN){ echo '#include "$*-signals-marshal.h"' && \
	glib-genmarshal --body --prefix=$(subst -,_,$*)_signals_marshal $< ; \
	} > $@

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
