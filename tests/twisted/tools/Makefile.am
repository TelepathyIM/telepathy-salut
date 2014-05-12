exec-with-log.sh: exec-with-log.sh.in
	$(AM_V_GEN)sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@
	@chmod +x $@

tmp-session-bus.conf: tmp-session-bus.conf.in
	$(AM_V_GEN)sed -e "s|[@]servicedir[@]|@abs_top_builddir@/tests/twisted/tools|g" $< > $@

servicedir = $(datadir)/dbus-1/services

installed/%.conf: %.conf.in Makefile
	@$(MKDIR_P) installed
	$(AM_V_GEN)sed -e 's|[@]servicedir[@]|$(servicedir)|g' $< > $@

# For the mock Avahi we need to set DBUS_SYSTEM_BUS_ADDRESS to the
# temporary session bus's address
mock-avahi/%.conf: %.conf.in Makefile
	@$(MKDIR_P) mock-avahi
	$(AM_V_GEN)sed -e "s|^Exec=.*|Exec=/bin/sh -c 'DBUS_SYSTEM_BUS_ADDRESS=$$DBUS_SESSION_BUS_ADDRESS ${libexecdir}/telepathy-salut-1'|g" $< > $@

# We don't use the full filename for the .in because > 99 character filenames
# in tarballs are non-portable (and automake 1.8 doesn't let us build
# non-archaic tarballs)
im.telepathy.v1.ConnectionManager.%.service: %.service.in
	$(AM_V_GEN)sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

mock-avahi/im.telepathy.v1.ConnectionManager.%.service: %.service.in
	$(AM_V_GEN)sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

# D-Bus service file for testing
service_in_files = salut.service.in
service_files = im.telepathy.v1.ConnectionManager.salut.service

# D-Bus config file for testing
conf_in_files = tmp-session-bus.conf.in
conf_files = $(conf_in_files:.conf.in=.conf)

BUILT_SOURCES = \
	$(built_test_data) \
	$(conf_files) \
	$(nobase_built_test_data) \
	$(service_files) \
	exec-with-log.sh \
	$(NULL)

dist_test_scripts = \
	with-session-bus.sh \
	$(NULL)

dist_test_data = \
	$(NULL)

built_test_data = \
	installed/tmp-session-bus.conf \
	$(NULL)

nobase_built_test_data = \
	mock-avahi/tmp-session-bus.conf \
	mock-avahi/im.telepathy.v1.ConnectionManager.salut.service \
	$(NULL)

EXTRA_DIST = \
	$(conf_in_files) \
	$(dist_test_data) \
	$(dist_test_scripts) \
	$(service_in_files) \
	exec-with-log.sh.in \
	run_and_bt.gdb

CLEANFILES = \
    $(BUILT_SOURCES) \
    salut-testing.log

testsdir = ${datadir}/telepathy-salut-1-tests
twistedtestsdir = ${testsdir}/twisted
twistedtoolsdir = ${twistedtestsdir}/tools

if ENABLE_INSTALLED_TESTS
dist_twistedtools_SCRIPTS = ${dist_test_scripts}
dist_twistedtools_DATA = ${dist_test_data}
twistedtools_DATA = ${built_test_data}
nobase_twistedtools_DATA = ${nobase_built_test_data}
endif
