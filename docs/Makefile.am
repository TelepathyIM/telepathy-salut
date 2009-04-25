include $(top_srcdir)/tools/shave.mk

tools_dir = $(top_srcdir)/tools

man_MANS = telepathy-salut.8
html_DATA = clique.html

EXTRA_DIST = \
    $(man_MANS:.8=.8.in) \
    $(html_DATA:.html=.xml)

CLEANFILES = \
    $(man_MANS) \
    $(html_DATA)

%.8: %.8.in Makefile
	$(QUIET_GEN)sed -e 's,[@]libexecdir[@],@libexecdir@,' < $< > $@

$(html_DATA): %.html: %.xml $(tools_dir)/xep.xsl
	$(QUIET_GEN)$(XSLTPROC) $(tools_dir)/xep.xsl $< > $@

proto-xep-upload: $(html_DATA)
	rsync -P $(html_DATA) people.collabora.co.uk:public_html/
.PHONY: proto-xep-upload
