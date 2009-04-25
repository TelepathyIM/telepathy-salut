include $(top_srcdir)/tools/shave.mk

tools_dir = $(top_srcdir)/tools

EXTRA_DIST = \
    Connection_Interface_Contact_Capabilities.xml \
    Channel_Type_Stream_Tube.xml \
    Channel_Type_DBus_Tube.xml \
    Channel_Interface_Tube.xml \
    channel.xml \
    OLPC_Buddy_Info.xml \
    OLPC_Activity_Properties.xml \
    connection.xml \
    all.xml

noinst_LTLIBRARIES = libsalut-extensions.la

libsalut_extensions_la_SOURCES = \
    extensions.c \
    extensions.h

nodist_libsalut_extensions_la_SOURCES = \
    _gen/signals-marshal.c \
    _gen/signals-marshal.h \
    _gen/signals-marshal.list \
    _gen/enums.h \
    _gen/gtypes.h \
    _gen/gtypes-body.h \
    _gen/interfaces.h \
    _gen/interfaces-body.h \
    _gen/svc.h \
    _gen/svc.c

BUILT_SOURCES = \
    _gen/all.xml \
    _gen/channel.xml \
    _gen/connection.xml \
    $(nodist_libsalut_extensions_la_SOURCES) \
    extensions.html

CLEANFILES = $(BUILT_SOURCES) _gen/.exists

AM_CFLAGS = \
    $(ERROR_CFLAGS) \
    $(GCOV_CFLAGS) \
    $(DBUS_CFLAGS) \
    $(GLIB_CFLAGS) \
    $(TELEPATHY_GLIB_CFLAGS)

AM_LDFLAGS = $(GCOV_LIBS) @DBUS_LIBS@ @GLIB_LIBS@ @TELEPATHY_GLIB_LIBS@

# Generated files which can be generated for all categories simultaneously

XSLTPROCFLAGS = --nonet --novalid

_gen/%.xml: %.xml $(wildcard *.xml) $(tools_dir)/identity.xsl
	$(mkdir_p) _gen
	$(QUIET_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude $(tools_dir)/identity.xsl \
		$< > $@

extensions.html: _gen/all.xml $(tools_dir)/doc-generator.xsl
	$(QUIET_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude \
		$(tools_dir)/doc-generator.xsl \
		$< > $@

_gen/signals-marshal.list: _gen/all.xml \
	$(tools_dir)/glib-signals-marshal-gen.py
	$(QUIET_GEN)$(PYTHON) $(tools_dir)/glib-signals-marshal-gen.py $< > $@

_gen/signals-marshal.h: _gen/signals-marshal.list Makefile.am
	$(QUIET_GEN)$(GLIB_GENMARSHAL) --header --prefix=_salut_ext_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list Makefile.am
	$(QUIET_GEN){ echo '#include "_gen/signals-marshal.h"' && \
	$(GLIB_GENMARSHAL) --body --prefix=_salut_ext_marshal $< ; } > $@

_gen/register-dbus-glib-marshallers-body.h: _gen/all.xml \
	$(tools_dir)/glib-client-marshaller-gen.py
	$(QUIET_GEN)$(PYTHON) $(tools_dir)/glib-client-marshaller-gen.py $< \
		_salut_ext > $@

_gen/enums.h: _gen/all.xml $(tools_dir)/c-constants-generator.xsl
	$(QUIET_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix Salut \
		$(tools_dir)/c-constants-generator.xsl \
		$< > $@

_gen/gtypes.h _gen/gtypes-body.h: _gen/all.xml \
	$(tools_dir)/glib-gtypes-generator.py Makefile.am
	$(QUIET_GEN)$(PYTHON) $(tools_dir)/glib-gtypes-generator.py \
		$< _gen/gtypes Salut

_gen/interfaces.h: _gen/all.xml \
	$(tools_dir)/glib-interfaces-generator.xsl \
	$(tools_dir)/c-interfaces-generator.xsl
	$(QUIET_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix Salut \
		$(tools_dir)/glib-interfaces-generator.xsl \
		$< > $@

_gen/interfaces-body.h: _gen/all.xml \
	$(tools_dir)/glib-interfaces-body-generator.xsl \
	$(tools_dir)/c-interfaces-generator.xsl
	$(QUIET_GEN)$(XSLTPROC) $(XSLTPROCFLAGS) \
		--stringparam mixed-case-prefix Salut \
		$(tools_dir)/glib-interfaces-body-generator.xsl \
		$< > $@

_gen/svc.c _gen/svc.h: _gen/all.xml $(tools_dir)/glib-ginterface-gen.py
	$(QUIET_GEN)$(PYTHON) $(tools_dir)/glib-ginterface-gen.py \
		--filename=_gen/svc \
		--signal-marshal-prefix=_salut_ext \
		--include='<telepathy-glib/dbus.h>' \
		--include='"_gen/signals-marshal.h"' \
		--not-implemented-func='tp_dbus_g_method_return_not_implemented' \
		--allow-unstable \
		$< Salut_Svc_
