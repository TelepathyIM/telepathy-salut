ACLOCAL_AMFLAGS = -I m4

SUBDIRS = lib src data m4 tests

dist-hook:
	chmod u+w ${distdir}/ChangeLog
	if test -d _darcs; then darcs changes >${distdir}/ChangeLog; fi
