ACLOCAL_AMFLAGS = -I m4

SUBDIRS = tools extensions lib src data m4 salut plugins tests docs

DISTCHECK_CONFIGURE_FLAGS = --disable-debug --enable-gtk-doc

valgrind:
	cd tests && $(MAKE) valgrind

torture:
	cd tests && $(MAKE) torture

forever:
	cd tests && $(MAKE) forever

include $(top_srcdir)/rules/lcov.mak

# There doesn't seem to be a telepathy-salut-0.4.0 tag! :o
CHANGELOG_RANGE = 22651d24..
CHECK_FOR_UNRELEASED = \
	$(srcdir)/NEWS \
	$(wildcard $(srcdir)/salut/*.[ch]) \
	$(wildcard $(srcdir)/src/*.[ch])

include tools/telepathy.am
