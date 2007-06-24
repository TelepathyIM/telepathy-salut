ACLOCAL_AMFLAGS = -I m4

SUBDIRS = extensions lib src data m4 tests

dist-hook:
	chmod u+w ${distdir}/ChangeLog
	if test -d _darcs; then darcs changes >${distdir}/ChangeLog; fi

valgrind:
	cd tests && make valgrind

torture:
	cd tests && make torture

forever:
	cd tests && make forever

.PHONY: lcov

include $(top_srcdir)/rules/lcov.mak
