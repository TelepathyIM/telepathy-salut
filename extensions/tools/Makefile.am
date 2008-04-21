EXTRA_DIST = \
    c-constants-generator.xsl \
    c-interfaces-generator.xsl \
    doc-generator.xsl \
    genginterface.py \
    glib-client-marshaller-gen.py \
    glib-errors-enum-body.xsl \
    glib-errors-enum-header.xsl \
    glib-interfaces-generator.xsl \
    glib-interfaces-body-generator.xsl \
    glib-ginterface-gen.py \
    glib-gtypes-generator.py \
    glib-signals-marshal-gen.py \
    identity.xsl \
    libglibcodegen.py \
    ls-interfaces.xsl \
    make-all-async.xsl \
    spec-to-introspect.xsl \
    update-spec-gen-am.sh

glib-client-marshaller-gen.py: libglibcodegen.py
	touch $@
glib-ginterface-gen.py: libglibcodegen.py
	touch $@
glib-gtypes-generator.py: libglibcodegen.py
	touch $@
glib-signals-marshal-gen.py: libglibcodegen.py
	touch $@

glib-interfaces-generator.xsl: c-interfaces-generator.xsl
	touch $@
glib-interfaces-body-generator.xsl: c-interfaces-generator.xsl
	touch $@

TELEPATHY_GLIB_SRCDIR = $(top_srcdir)/../telepathy-glib
maintainer-update-from-telepathy-glib:
	set -e && cd $(srcdir) && \
	for x in $(EXTRA_DIST); do \
		if test -f $(TELEPATHY_GLIB_SRCDIR)/tools/$$x; then \
			cp $(TELEPATHY_GLIB_SRCDIR)/tools/$$x $$x; \
		fi; \
	done
