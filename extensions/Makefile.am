if ENABLE_OLPC

tools_dir = $(srcdir)/../lib/tools

# This is a derived file but must be checked-in to Darcs anyway,
# for bootstrapping.
spec-gen.am: Makefile.am all.xml $(SPEC_INTERFACE_XMLS) \
	$(tools_dir)/ls-interfaces.xsl
	$(XSLTPROC) $(XSLTPROCFLAGS) --xinclude \
		$(tools_dir)/ls-interfaces.xsl \
		$(srcdir)/all.xml > spec-gen.tmp
	@echo Interfaces to be generated are:
	@cat spec-gen.tmp ; echo
	@echo end of interfaces list
	(\
	echo SPEC_INTERFACES = `cat spec-gen.tmp` && \
	echo SPEC_INTERFACE_XMLS = `sed -e 's,[A-Za-z_][A-Za-z_]*,./&.xml,g' spec-gen.tmp` && \
	echo SPEC_GENERATED_CS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&.c,g' spec-gen.tmp` && \
	echo SPEC_GENERATED_HS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&.h,g' spec-gen.tmp` && \
	echo SPEC_GLUE_HS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&-glue.h,g' spec-gen.tmp` && \
	echo SPEC_GENERATED_LISTS = `sed -e 's,[A-Za-z_][A-Za-z_]*,_gen/svc-&-signals-marshal.list,g' spec-gen.tmp` \
	) > spec-gen.am
	rm -f spec-gen.tmp

include spec-gen.am

SPEC_GENERATED_SOURCES = $(SPEC_GENERATED_CS) $(SPEC_GENERATED_HS) \
			 $(SPEC_GENERATED_LISTS) $(SPEC_GLUE_HS)

noinst_LTLIBRARIES = libsalut-extensions.la

nodist_libsalut_extensions_la_SOURCES = \
    _gen/signals-marshal.c \
    _gen/signals-marshal.h \
    _gen/signals-marshal.list \
    $(SPEC_GENERATED_SOURCES)

BUILT_SOURCES = $(nodist_libsalut_extensions_la_SOURCES)

CLEANFILES = $(BUILT_SOURCES) _gen/.exists

clean-local:
	-rmdir _gen

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ \
	           @TELEPATHY_GLIB_CFLAGS@

AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @TELEPATHY_GLIB_LIBS@
# Generated stuff

DROP_NAMESPACE = sed -e 's@xmlns:tp="http://telepathy\.freedesktop\.org/wiki/DbusSpec.extensions-v0"@@g'
XSLTPROCFLAGS = --nonet --novalid

_gen/.exists:
	$(INSTALL) -d _gen
	touch $@

_gen/%.xml: %.xml $(tools_dir)/spec-to-introspect.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) \
		$(tools_dir)/spec-to-introspect.xsl $< \
		| $(DROP_NAMESPACE) > $@

_gen/async-%.xml: _gen/%.xml $(tools_dir)/make-all-async.xsl _gen/.exists
	$(XSLTPROC) $(XSLTPROCFLAGS) $(tools_dir)/make-all-async.xsl $< > $@

_gen/svc-%-glue.h: _gen/async-%.xml _gen/.exists
	$(DBUS_BINDING_TOOL) --mode=glib-server --output=$@ \
		--prefix=tp_svc_`echo $* | tr A-Z a-z` $<

_gen/svc-%.c _gen/svc-%.h _gen/svc-%-signals-marshal.list: %.xml $(tools_dir)/genginterface.py _gen/.exists
	$(PYTHON) $(tools_dir)/genginterface.py $< \
	TpSvc`echo $* | tr -d _` _gen/svc-$* _salut_ext

_gen/signals-marshal.list: $(SPEC_GENERATED_LISTS)
	sort $^ | uniq > $@

_gen/signals-marshal.h: _gen/signals-marshal.list _gen/.exists
	$(GLIB_GENMARSHAL) --header --prefix=_salut_ext_marshal $< > $@

_gen/signals-marshal.c: _gen/signals-marshal.list _gen/.exists
	$(GLIB_GENMARSHAL) --body --prefix=_salut_ext_marshal $< > $@

endif
