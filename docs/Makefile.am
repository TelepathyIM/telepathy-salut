PROTO_XEP_XML = \
    clique.xml
PROTO_XEP_HTML = $(PROTO_XEP_XML:.xml=.html)

proto-xep-html: $(PROTO_XEP_HTML)
.PHONY: proto-xep-html

$(PROTO_XEP_HTML): %.html: %.xml xep.xsl
	$(XSLTPROC) xep.xsl $< > $@
.PHONY: proto-xep-html

proto-xep-upload: proto-xep-html
	rsync -P $(PROTO_XEP_HTML) projects.collabora.co.uk:public_html/
.PHONY: proto-xep-upload

xep.xsl:
	@echo "Please get xep.xsl from xmpp.org svn - see README for details"
	@echo "This command might work:"
	@echo "wget 'http://svn.xmpp.org:18080/browse/~raw,r=1214/XMPP/trunk/extensions/xep.xsl'"
	@exit 1

EXTRA_DIST = $(PROTO_XEP_XML)
