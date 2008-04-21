EXTRA_DIST = salut.service.in

managerdir = $(datadir)/telepathy/managers
manager_DATA = salut.manager

servicedir = $(datadir)/dbus-1/services
service_DATA = salut.service.in

org.freedesktop.Telepathy.ConnectionManager.salut.service: salut.service.in \
		Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

CLEANFILES = $(service_DATA) $(manager_DATA)

$(manager_DATA): always-build
	$(MAKE) -C ../src write-mgr-file
	../src/write-mgr-file > $@

.PHONY: always-build
