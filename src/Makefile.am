BUILT_SOURCES = \
    signals-marshal.list \
    signals-marshal.h \
    signals-marshal.c \
    salut-presence-enumtypes.h \
    salut-presence-enumtypes.c  \
    salut-avahi-client-enumtypes.h \
    salut-avahi-client-enumtypes.c  \
    salut-avahi-entry-group-enumtypes.h \
    salut-avahi-entry-group-enumtypes.c  \
    salut-avahi-enums-enumtypes.h \
    salut-avahi-enums-enumtypes.c


# correctly clean the generated source files
CLEANFILES = $(BUILT_SOURCES)

dist-hook:
	$(shell for x in $(BUILT_SOURCES); do rm -f $(distdir)/$$x ; done)

bin_PROGRAMS=telepathy-salut
noinst_PROGRAMS = write-mgr-file

CORE_SOURCES =                                          \
    salut-connection-manager.c                          \
    salut-connection-manager.h                          \
    salut-contact-manager.c                             \
    salut-contact-manager.h                             \
    salut-im-manager.c                                  \
    salut-im-manager.h                                  \
    salut-im-channel.c                                  \
    salut-im-channel.h                                  \
    salut-muc-manager.c                                 \
    salut-muc-manager.h                                 \
    salut-muc-channel.c                                 \
    salut-muc-channel.h                                 \
    salut-contact.h                                     \
    salut-contact.c                                     \
    salut-self.h                                        \
    salut-self.c                                        \
    salut-contact-channel.c                             \
    salut-contact-channel.h                             \
    salut-avahi-errors.c                                \
    salut-avahi-errors.h                                \
    salut-avahi-client.c                                \
    salut-avahi-client.h                                \
    salut-avahi-entry-group.c                           \
    salut-avahi-entry-group.h                           \
    salut-avahi-record-browser.c                        \
    salut-avahi-record-browser.h                        \
    salut-avahi-service-browser.c                       \
    salut-avahi-service-browser.h                       \
    salut-avahi-service-resolver.c                      \
    salut-avahi-service-resolver.h                      \
    salut-avahi-enums.h                                 \
    salut-connection.c                                  \
    salut-connection.h                                  \
    salut-presence.h                                    \
    salut-contact-channel.h                             \
    salut-contact-channel.c                             \
    text-helper.c                                       \
    text-helper.h                                       \
    tube-dbus.h                                         \
    tube-dbus.c                                         \
    salut-tubes-channel.h                               \
    salut-tubes-channel.c                               \
    tube-iface.h                                        \
    tube-iface.c                                        \
    sha1/sha1-util.h                                    \
    sha1/sha1-util.c                                    \
    sha1/sha1.h                                         \
    sha1/sha1.c                                         \
    salut-util.h                                        \
    salut-util.c                                        \
    debug.c                                             \
    debug.h                                             \
    ansi.h                                              \
    namespaces.h


libsalut_convenience_la_SOURCES = $(CORE_SOURCES) $(BUILT_SOURCES)

write_mgr_file_SOURCES = write-mgr-file.c
write_mgr_file_LDADD = libsalut-convenience.la \
                       $(top_builddir)/lib/gibber/libgibber.la \
                       $(top_builddir)/extensions/libsalut-extensions.la \
                       -ltelepathy-glib
telepathy_salut_SOURCES = \
    salut.c 

# TELEPATHY_GLIB_LIBS is only here so compiling against a static lib succeeds
telepathy_salut_LDADD = libsalut-convenience.la \
                        $(top_builddir)/lib/gibber/libgibber.la \
                        $(top_builddir)/extensions/libsalut-extensions.la \
                        -ltelepathy-glib

noinst_LTLIBRARIES = libsalut-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) $(GCOV_CFLAGS) @DBUS_CFLAGS@ \
  @GLIB_CFLAGS@ @LIBXML2_CFLAGS@ \
  @TELEPATHY_GLIB_CFLAGS@ \
  @AVAHI_CFLAGS@ \
  -I $(top_srcdir) -I $(top_builddir) \
  -I $(top_srcdir)/lib -I $(top_builddir)/lib

AM_LDFLAGS = $(GCOV_LIBS) @DBUS_LIBS@ \
             @TELEPATHY_GLIB_LIBS@ \
             @GLIB_LIBS@ @LIBXML2_LIBS@ @AVAHI_LIBS@

# Teach it how to make libgibber.la
$(top_builddir)/lib/gibber/libgibber.la:
	${MAKE} -C $(top_builddir)/lib/gibber libgibber.la

.PHONY: $(top_builddir)/lib/gibber/libgibber.la


signals-marshal.list: $(CORE_SOURCES) Makefile.am
	( cd $(srcdir) && \
	sed -n -e 's/.*salut_signals_marshal_\([A-Z]*__[A-Z_]*\).*/\1/p' \
		$(CORE_SOURCES) ) \
		| sed -e 's/__/:/' -e 'y/_/,/' | sort -u > $@.tmp
	if cmp -s $@.tmp $@; then \
		rm $@.tmp; \
	else \
		mv $@.tmp $@; \
	fi

signals-marshal.h: signals-marshal.list
	glib-genmarshal --header --prefix=salut_signals_marshal $< > $@

signals-marshal.c: signals-marshal.list
	glib-genmarshal --body --prefix=salut_signals_marshal $< > $@


# rules for making the glib enum objects
%-enumtypes.h: %.h Makefile.in
	glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define $(shell echo $* | tr [:lower:]- [:upper:]_ | sed 's/_.*//')_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
	$< > $@

%-enumtypes.c: %.h Makefile.in
	glib-mkenums \
	--fhead "#include <$*.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@VALUENAME@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$< > $@
