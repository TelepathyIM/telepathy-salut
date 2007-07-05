ACLOCAL_AMFLAGS = -I m4

SUBDIRS = extensions lib src data m4 tests

dist-hook:
	chmod u+w ${distdir}/ChangeLog
	if test -d _darcs; then darcs changes >${distdir}/ChangeLog; fi

valgrind:
	cd tests && $(MAKE) valgrind

torture:
	cd tests && $(MAKE) torture

forever:
	cd tests && $(MAKE) forever

include $(top_srcdir)/rules/lcov.mak
