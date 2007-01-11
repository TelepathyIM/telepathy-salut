BUILT_SOURCES = \
    salut-connection-manager-glue.h \
    salut-connection-manager-signals-marshal.h \
    salut-connection-manager-signals-marshal.c \
    salut-contact-manager-signals-marshal.h \
    salut-contact-manager-signals-marshal.c \
    salut-im-manager-signals-marshal.h \
    salut-im-manager-signals-marshal.c \
    salut-im-channel-glue.h \
    salut-im-channel-signals-marshal.h \
    salut-im-channel-signals-marshal.c \
    salut-lm-connection-signals-marshal.h \
    salut-lm-connection-signals-marshal.c \
    salut-self-signals-marshal.h \
    salut-self-signals-marshal.c \
    salut-contact-signals-marshal.h \
    salut-contact-signals-marshal.c \
    salut-connection-glue.h \
    salut-connection-signals-marshal.h \
    salut-connection-signals-marshal.c \
    salut-transport-mixin-signals-marshal.h \
    salut-transport-mixin-signals-marshal.c \
    salut-presence-enumtypes.h \
    salut-presence-enumtypes.c  \
    telepathy-errors-enumtypes.h \
    telepathy-errors-enumtypes.c  \
    tp-channel-factory-iface-signals-marshal.h \
    tp-channel-factory-iface-signals-marshal.c \
    group-mixin-signals-marshal.h \
    group-mixin-signals-marshal.c \
    text-mixin-signals-marshal.h \
    text-mixin-signals-marshal.c \
    salut-contact-channel-glue.h \
    salut-contact-channel-signals-marshal.h \
    salut-contact-channel-signals-marshal.c  \
    salut-avahi-client-signals-marshal.h \
    salut-avahi-client-signals-marshal.c  \
    salut-avahi-client-enumtypes.h \
    salut-avahi-client-enumtypes.c  \
    salut-avahi-entry-group-enumtypes.h \
    salut-avahi-entry-group-enumtypes.c  \
    salut-avahi-enums-enumtypes.h \
    salut-avahi-enums-enumtypes.c  \
    salut-avahi-entry-group-signals-marshal.h \
    salut-avahi-entry-group-signals-marshal.c  \
    salut-avahi-service-browser-signals-marshal.h \
    salut-avahi-service-browser-signals-marshal.c  \
    salut-avahi-service-resolver-signals-marshal.h \
    salut-avahi-service-resolver-signals-marshal.c 


# correctly clean the generated source files
CLEANFILES = $(BUILT_SOURCES)

dist-hook:
	$(shell for x in $(BUILT_SOURCES); do rm -f $(distdir)/$$x ; done)

EXTRA_DIST = \
   group-mixin-signals-marshal.list                     \
   text-mixin-signals-marshal.list                      \
   salut-avahi-client-signals-marshal.list              \
   salut-avahi-entry-group-signals-marshal.list         \
   salut-avahi-service-browser-signals-marshal.list     \
   salut-avahi-service-resolver-signals-marshal.list    \
   salut-connection-manager-signals-marshal.list        \
   salut-connection-signals-marshal.list                \
   salut-contact-channel-signals-marshal.list           \
   salut-contact-manager-signals-marshal.list           \
   salut-contact-signals-marshal.list                   \
   salut-im-channel-signals-marshal.list                \
   salut-im-manager-signals-marshal.list                \
   salut-lm-connection-signals-marshal.list             \
   salut-self-signals-marshal.list                      \
   tp-channel-factory-iface-signals-marshal.list
 

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
    salut-lm-connection.c                               \
    salut-lm-connection.h                               \
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
    salut-avahi-service-browser.c                       \
    salut-avahi-service-browser.h                       \
    salut-avahi-service-resolver.c                      \
    salut-avahi-service-resolver.h                      \
    salut-avahi-enums.h                                 \
    salut-connection.c                                  \
    salut-connection.h                                  \
    salut-presence.h                                    \
    salut-xmpp-stanza.c                                 \
    salut-xmpp-stanza.h                                 \
    salut-xmpp-node.c                                   \
    salut-xmpp-node.h                                   \
    salut-transport-mixin.c                             \
    salut-transport-mixin.h                             \
    telepathy-errors.c                                  \
    telepathy-errors.h                                  \
    telepathy-constants.h                               \
    telepathy-helpers.c                                 \
    telepathy-helpers.h                                 \
    telepathy-interfaces.h                              \
    gintset.c                                           \
    gintset.h                                           \
    gheap.c                                             \
    gheap.h                                             \
    util.c                                              \
    util.h                                              \
    handle-set.c                                        \
    handle-set.h                                        \
    handle-types.h                                      \
    handle-repository.c                                 \
    handle-repository.h                                 \
    tp-channel-iface.h                                  \
    tp-channel-iface.c                                  \
    tp-channel-factory-iface.h                          \
    tp-channel-factory-iface.c                          \
    salut-contact-channel.h                             \
    salut-contact-channel.c                             \
    group-mixin.c                                       \
    group-mixin.h                                       \
    text-mixin.c                                        \
    text-mixin.h                                        \
    debug.c                                             \
    debug.h                                             \
    ansi.h                                              \
    lm-parser.h                                         \
    $(BUILT_SOURCES)

libsalut_convenience_la_SOURCES = $(CORE_SOURCES)

write_mgr_file_SOURCES = write-mgr-file.c
write_mgr_file_LDADD = libsalut-convenience.la

telepathy_salut_SOURCES = \
    salut.c 

telepathy_salut_LDADD = libsalut-convenience.la

noinst_LTLIBRARIES = libsalut-convenience.la

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ @HANDLE_LEAK_DEBUG_CFLAGS@ @AVAHI_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@ @AVAHI_LIBS@

# rule to generate the binding headers
%-glue.h: ../generate/xml-modified/%.xml
	dbus-binding-tool --mode=glib-server --output=$@ --prefix=$(subst -,_,$*) $<

%-signals-marshal.h: %-signals-marshal.list
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.h

%-signals-marshal.c: %-signals-marshal.list
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.c

%-marshal.h: %-marshal.list
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.h

%-marshal.c: %-marshal.list
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.c


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
