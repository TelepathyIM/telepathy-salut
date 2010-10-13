ACLOCAL_AMFLAGS = -I m4

SUBDIRS = tools extensions lib src data m4 tests docs

DISTCHECK_CONFIGURE_FLAGS = --disable-debug --enable-gtk-doc

dist-hook:
	chmod u+w ${distdir}/ChangeLog
	if test -d ${top_srcdir}/.git; then \
		git log --stat > ${distdir}/ChangeLog || \
		git log > ${distdir}/ChangeLog; \
	fi

valgrind:
	cd tests && $(MAKE) valgrind

torture:
	cd tests && $(MAKE) torture

forever:
	cd tests && $(MAKE) forever

include $(top_srcdir)/rules/lcov.mak
