EXTRA_DIST = salut.service.in

managerdir = $(datadir)/telepathy/managers
manager_DATA = salut.manager

servicedir = $(datadir)/dbus-1/services
service_DATA = org.freedesktop.Telepathy.ConnectionManager.salut.service

# We don't use the full filename for the .in because > 99 character filenames
# in tarballs are non-portable (and automake 1.8 doesn't let us build
# non-archaic tarballs)
org.freedesktop.Telepathy.ConnectionManager.salut.service: salut.service.in \
		Makefile
	$(AM_V_GEN)sed -e "s|[@]libexecdir[@]|$(libexecdir)|" $< > $@

CLEANFILES = $(service_DATA) $(manager_DATA)

$(manager_DATA): always-build
	$(MAKE) -C ../src write-mgr-file$(EXEEXT)
	$(AM_V_GEN)../src/write-mgr-file$(EXEEXT) > $@

.PHONY: always-build
