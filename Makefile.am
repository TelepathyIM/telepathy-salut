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

LCOV_EXECUTABLES=scripts/lcov/lcov		\
                 scripts/lcov/gendesc	\
                 scripts/lcov/genhtml	\
                 scripts/lcov/geninfo	\
                 scripts/lcov/genpng

EXTRA_DIST=$(LCOV_EXECUTABLES)

.PHONY: always-run

$(LCOV_EXECUTABLES): always-run
	chmod +x $(srcdir)/$@

.PHONY: lcov

lcov: $(LCOV_EXECUTABLES)

include $(top_srcdir)/rules/lcov.mak
