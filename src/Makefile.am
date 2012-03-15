BUILT_SOURCES = \
    signals-marshal.list \
    signals-marshal.h \
    signals-marshal.c \
    enumtypes.h \
    enumtypes.c

# correctly clean the generated source files
CLEANFILES = $(BUILT_SOURCES)

dist-hook:
	$(shell for x in $(BUILT_SOURCES); do rm -f $(distdir)/$$x ; done)

libexec_PROGRAMS=telepathy-salut
noinst_PROGRAMS = write-mgr-file

CORE_SOURCES =                                          \
    symbol-hacks.c \
    symbol-hacks.h \
    capability-set.c \
    $(top_srcdir)/salut/capabilities.h \
    $(top_srcdir)/salut/capability-set.h \
    gabble_namespaces.h \
    namespaces.h \
    capabilities.c                                \
    capabilities.h                                \
    caps-hash.c                                   \
    caps-hash.h                                   \
    connection-manager.c                          \
    connection-manager.h                          \
    contact-manager.c                             \
    contact-manager.h                             \
    disco.c                                       \
    disco.h                                       \
    im-manager.c                                  \
    im-manager.h                                  \
    im-channel.c                                  \
    im-channel.h                                  \
    muc-manager.c                                 \
    muc-manager.h                                 \
    roomlist-manager.c                            \
    roomlist-manager.h                            \
    muc-channel.c                                 \
    ft-manager.c                                  \
    ft-manager.h                                  \
    file-transfer-channel.c                       \
    file-transfer-channel.h                       \
    muc-channel.h                                 \
    presence-cache.c                              \
    presence-cache.h                              \
    tubes-manager.c                               \
    tubes-manager.h                               \
    contact.h                                     \
    contact.c                                     \
    self.h                                        \
    self.c                                        \
    connection.c                                  \
    connection.h                                  \
    connection-contact-info.c                     \
    connection-contact-info.h                     \
    presence.h                                    \
    si-bytestream-manager.h                       \
    si-bytestream-manager.c                       \
    text-helper.c                                 \
    text-helper.h                                 \
    roomlist-channel.h                            \
    roomlist-channel.c                            \
    discovery-client.h                            \
    discovery-client.c                            \
    tube-dbus.h                                   \
    tube-dbus.c                                   \
    tube-iface.h                                  \
    tube-iface.c                                  \
    tube-stream.h                                 \
    tube-stream.c                                 \
    tubes-channel.h                               \
    tubes-channel.c                               \
    util.h                                        \
    util.c                                        \
    protocol.c                                          \
    $(top_srcdir)/salut/protocol.h                      \
    plugin-loader.c                                     \
    plugin-loader.h

AVAHI_BACKEND_SOURCES = 				\
    avahi-discovery-client.h                      \
    avahi-discovery-client.c                      \
    avahi-muc-manager.h                           \
    avahi-muc-manager.c                           \
    avahi-roomlist-manager.h                      \
    avahi-roomlist-manager.c                      \
    avahi-muc-channel.h                           \
    avahi-muc-channel.c                           \
    avahi-contact-manager.h                       \
    avahi-contact-manager.c                       \
    avahi-contact.h                               \
    avahi-contact.c                               \
    avahi-self.h                                  \
    avahi-self.c

BONJOUR_BACKEND_SOURCES =                         \
    bonjour-self.h                                \
    bonjour-self.c                                \
    bonjour-contact-manager.h                     \
    bonjour-contact-manager.c                     \
    bonjour-discovery-client.h                    \
    bonjour-discovery-client.c

if ENABLE_OLPC
  CORE_SOURCES +=                                       \
    olpc-activity.h                               \
    olpc-activity.c                               \
    olpc-activity-manager.h                       \
    olpc-activity-manager.c

  AVAHI_BACKEND_SOURCES +=                              \
    avahi-olpc-activity-manager.h                 \
    avahi-olpc-activity-manager.c                 \
    avahi-olpc-activity.h                         \
    avahi-olpc-activity.c
endif

DUMMY_BACKEND_SOURCES = 				\
    dummy-discovery-client.h                      \
    dummy-discovery-client.c

SHA1_SOURCES =                                          \
    sha1/sha1-util.h                                    \
    sha1/sha1-util.c

lib_LTLIBRARIES = libsalut-plugins.la

libsalut_plugins_la_LDFLAGS =                           \
    -shared                                             \
    -no-undefined                                       \
    -release $(VERSION)

libsalut_plugins_android_la_LDFLAGS =                   \
    -shared                                             \
    -avoid-version
    
libsalut_plugins_la_LIBADD =                            \
    @TELEPATHY_GLIB_LIBS@                               \
    @WOCKY_LIBS@

libsalut_plugins_la_SOURCES =                           \
    caps-channel-manager.c                              \
    $(top_srcdir)/salut/caps-channel-manager.h          \
    $(top_srcdir)/salut/plugin.h                        \
    plugin-util.c                                       \
    $(top_srcdir)/salut/util.h                          \
    plugin.c                                            \
    $(top_srcdir)/salut/plugin-connection.h             \
    plugin-connection.c                                 \
    $(top_srcdir)/salut/plugin-contact.h                \
    plugin-contact.c                                    \
    $(top_srcdir)/salut/capability-set.h                \
    capability-set.c                                    \
    debug.h                                             \
    debug.c                                             \
    $(top_srcdir)/salut/sidecar.h                       \
    sidecar.c

libsalut_convenience_la_LIBADD = libsalut-plugins.la

libsalut_convenience_la_SOURCES = $(CORE_SOURCES) $(BUILT_SOURCES) \
    $(SHA1_SOURCES) $(DUMMY_BACKEND_SOURCES)

if USE_BACKEND_AVAHI
  libsalut_convenience_la_SOURCES += $(AVAHI_BACKEND_SOURCES)
endif

if USE_BACKEND_BONJOUR
  libsalut_convenience_la_SOURCES += $(BONJOUR_BACKEND_SOURCES)
endif

write_mgr_file_SOURCES = write-mgr-file.c
write_mgr_file_LDADD = libsalut-convenience.la \
                       $(top_builddir)/lib/gibber/libgibber.la \
                       $(top_builddir)/extensions/libsalut-extensions.la \
                       @TELEPATHY_GLIB_LIBS@
telepathy_salut_SOURCES = \
    salut.c

telepathy_salut_LDFLAGS = -export-dynamic

# Coding style checks
check_c_sources = \
    $(telepathy_salut_SOURCES) \
    $(CORE_SOURCES) \
    $(AVAHI_BACKEND_SOURCES) \
    $(BONJOUR_BACKEND_SOURCES) \
    $(DUMMY_BACKEND_SOURCES) \
    $(libsalut_plugins_la_SOURCES) \
    $(write_mgr_file_SOURCES)

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

# TELEPATHY_GLIB_LIBS is only here so compiling against a static lib succeeds
telepathy_salut_android_libs = libsalut-convenience.la \
                               -lgibber-salut \
                               $(top_builddir)/extensions/libsalut-extensions.la \
                               -ltelepathy-glib

telepathy_salut_LDADD = libsalut-convenience.la \
                        $(top_builddir)/lib/gibber/libgibber.la \
                        $(top_builddir)/extensions/libsalut-extensions.la \
                        -ltelepathy-glib

noinst_LTLIBRARIES = libsalut-convenience.la

AM_CFLAGS = \
	-I $(top_srcdir) -I $(top_builddir) \
	-I $(top_srcdir)/lib -I $(top_builddir)/lib \
	-I $(top_srcdir)/salut \
	-DG_LOG_DOMAIN=\"salut\" \
	-DPLUGIN_DIR=\"$(libdir)/telepathy/salut-0\" \
	$(ERROR_CFLAGS) \
	$(GCOV_CFLAGS) \
	@LIBXML2_CFLAGS@ \
	@GLIB_CFLAGS@ \
	@GIO_UNIX_CFLAGS@ \
	@GMODULE_CFLAGS@ \
	@WOCKY_CFLAGS@ \
	@DBUS_CFLAGS@ \
	@TELEPATHY_GLIB_CFLAGS@ \
	@UUID_CFLAGS@ \
	@LIBSOUP_CFLAGS@ \
	$(NULL)

AM_LDFLAGS = \
	$(GCOV_LIBS) \
	@LIBXML2_LIBS@ \
	@GLIB_LIBS@ \
	@GMODULE_LIBS@ \
	@WOCKY_LIBS@ \
	@DBUS_LIBS@ \
	@TELEPATHY_GLIB_LIBS@ \
	@UUID_LIBS@ \
	@LIBSOUP_LIBS@ \
	$(NULL)

if OS_WINDOWS
  AM_CFLAGS += -D_WIN32_WINNT=0x0501 \
      @GIO_WINDOWS_CFLAGS@
  AM_LDFLAGS += @GIO_WINDOWS_LIBS@
else
  AM_CFLAGS += @GIO_UNIX_CFLAGS@
  AM_LDFLAGS += @GIO_UNIX_LIBS@
endif

if USE_BACKEND_AVAHI
  AM_LDFLAGS += @AVAHI_LIBS@
  AM_CFLAGS += @AVAHI_CFLAGS@
endif

if USE_BACKEND_BONJOUR
  AM_LDFLAGS += @BONJOUR_LIBS@
  AM_CFLAGS += @BONJOUR_CFLAGS@
endif

# Teach it how to make libgibber.la
$(top_builddir)/lib/gibber/libgibber.la:
	${MAKE} -C $(top_builddir)/lib/gibber libgibber.la

.PHONY: $(top_builddir)/lib/gibber/libgibber.la

SIGNAL_MARSHAL_SOURCES = \
    $(CORE_SOURCES) \
    $(DUMMY_BACKEND_SOURCES)

if USE_BACKEND_AVAHI
  SIGNAL_MARSHAL_SOURCES += $(AVAHI_BACKEND_SOURCES)
endif

if USE_BACKEND_BONJOUR
    SIGNAL_MARSHAL_SOURCES += $(BONJOUR_BACKEND_SOURCES)
endif

signals-marshal.list: $(SIGNAL_Marshal_SOURCES) Makefile.am
	$(AM_V_GEN)( cd $(srcdir) && \
	sed -n -e 's/.*salut_signals_marshal_\([[:upper:][:digit:]]*__[[:upper:][:digit:]_]*\).*/\1/p' \
	$(SIGNAL_MARSHAL_SOURCES) ) \
	| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
	else \
		mv $@.tmp $@; \
	fi

signals-marshal.h: signals-marshal.list Makefile.am
	$(AM_V_GEN)glib-genmarshal --header --prefix=salut_signals_marshal $< > $@

signals-marshal.c: signals-marshal.list Makefile.am
	$(AM_V_GEN){ echo '#include "signals-marshal.h"' && \
	glib-genmarshal --body --prefix=salut_signals_marshal $< ; \
	} > $@

# rules for making the glib enum objects
enumtype_sources = \
    $(top_srcdir)/src/presence.h

enumtypes.h: $(enumtype_sources) Makefile.in
	$(AM_V_GEN)glib-mkenums \
	--fhead "#ifndef __SALUT_ENUM_TYPES_H__\n#define __SALUT_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define SALUT_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __SALUT_ENUM_TYPES_H__ */" \
	$(enumtype_sources) > $@

enumtypes.c: $(enumtype_sources) Makefile.in
	$(AM_V_GEN)glib-mkenums \
	--fhead "#include <$*.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */\n#include \"@filename@\"" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$(enumtype_sources) > $@

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer -:PROJECT telepathy-salut \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SHARED salut-plugins -:TAGS eng debug \
	 -:SOURCES $(libsalut_plugins_la_SOURCES)  \
	 -:CFLAGS $(DEFS) $(CFLAGS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	  $(AM_CFLAGS) \
	 -:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) \
	 -:LDFLAGS $(libsalut_plugins_android_la_LDFLAGS) $(AM_LDFLAGS) \
	 $(ALL_LIBS) \
	 -:STATIC salut-convenience -:TAGS eng debug \
	 -:SOURCES $(libsalut_convenience_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(CFLAGS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	 $(AM_CFLAGS) \
	 -:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) \
	 -:SHARED telepathy-salut \
	 -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(telepathy_salut_SOURCES) \
	 -:CFLAGS $(DEFS) $(CFLAGS) $(DEFAULT_INCLUDES) $(INCLUDES) \
	  $(AM_CFLAGS) \
	 -:CPPFLAGS $(CPPFLAGS) $(AM_CPPFLAGS) -DBUILD_AS_ANDROID_SERVICE \
	 -:LDFLAGS $(telepathy_salut_android_libs) $(AM_LDFLAGS) \
	 $(lib_LTLIBRARIES) \
	 -:LIBFILTER_STATIC salut-convenience \
	> $@
