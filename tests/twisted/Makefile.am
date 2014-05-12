TWISTED_TESTS =

TWISTED_BASIC_TESTS = \
	cm/protocol.py \
	$(NULL)

TWISTED_AVAHI_TESTS = \
	caps_helper.py \
	sidecars.py \
	avahi/register.py \
	avahi/aliases.py \
	avahi/request-im.py \
	avahi/muc-invite.py \
	avahi/caps-file-transfer.py \
	avahi/close-local-pending-room.py \
	avahi/only-text-muc-when-needed.py \
	avahi/file-transfer/send-file-and-cancel-immediately.py \
	avahi/file-transfer/send-file-and-disconnect.py \
	avahi/file-transfer/send-file-declined.py \
	avahi/file-transfer/send-file-item-not-found.py \
	avahi/file-transfer/send-file-ipv6.py \
	avahi/file-transfer/send-file-ipv4.py \
	avahi/file-transfer/send-file-provide-immediately.py \
	avahi/file-transfer/send-file-to-unknown-contact.py \
	avahi/file-transfer/send-file-wait-to-provide.py \
	avahi/file-transfer/receive-and-send-file.py \
	avahi/file-transfer/receive-file.py \
	avahi/file-transfer/receive-file-and-disconnect.py \
	avahi/file-transfer/receive-file-and-sender-disconnect-while-pending.py \
	avahi/file-transfer/receive-file-and-sender-disconnect-while-transfering.py \
	avahi/file-transfer/receive-file-and-xmpp-disconnect.py \
	avahi/file-transfer/receive-file-cancelled-immediately.py \
	avahi/file-transfer/receive-file-decline.py \
	avahi/file-transfer/receive-file-ipv6.py \
	avahi/file-transfer/receive-file-ipv4.py \
	avahi/file-transfer/receive-file-not-found.py \
	avahi/file-transfer/metadata.py \
	avahi/file-transfer/ft-client-caps.py \
	avahi/caps-self.py \
	avahi/text-channel.py \
	avahi/ichat-composing.py \
	avahi/ichat-incoming-msg.py \
	avahi/file-transfer/ichat-receive-directory.py \
	avahi/file-transfer/ichat-receive-file.py \
	avahi/file-transfer/ichat-send-file.py \
	avahi/file-transfer/ichat-send-file-declined.py  \
	avahi/request-muc.py \
	avahi/tubes/request-muc-tubes.py \
	avahi/roomlist.py \
	avahi/set-presence.py \
	avahi/tubes/two-muc-stream-tubes.py \
	avahi/tubes/two-muc-dbus-tubes.py

if WANT_TWISTED_TESTS
  TWISTED_TESTS += $(TWISTED_BASIC_TESTS)
if USE_BACKEND_AVAHI
  TWISTED_TESTS += $(TWISTED_AVAHI_TESTS)
endif
endif

if WANT_TWISTED_TESTS
check-local: check-coding-style check-twisted
else
check-local: check-coding-style
endif

CHECK_TWISTED_SLEEP=0

check-twisted: $(BUILT_SOURCES)
	$(MAKE) -C tools
	if test "x$(CHECK_TWISTED_SLEEP)" = x0; then \
		salut_test_sleep= ; \
	else \
		salut_test_sleep=--sleep=$(CHECK_TWISTED_SLEEP); \
	fi; \
	CHECK_TWISTED_UNINSTALLED=1 \
	  G_TEST_SRCDIR=@abs_srcdir@ \
	  G_TEST_BUILDDIR=@abs_builddir@ \
	  CHECK_TWISTED_SLEEP=$$salut_test_sleep \
	  ./run-test.sh "$(TWISTED_TESTS)"

if ENABLE_PLUGINS
PLUGINS_ENABLED_PYBOOL = True
else
PLUGINS_ENABLED_PYBOOL = False
endif

config.py: Makefile
	$(AM_V_GEN) { \
		echo "PACKAGE_STRING = \"$(PACKAGE_STRING)\""; \
		echo "PLUGINS_ENABLED = $(PLUGINS_ENABLED_PYBOOL)"; \
	} > $@

twisted-tests.list: Makefile
	$(AM_V_GEN)echo $(TWISTED_TESTS) > $@

BUILT_SOURCES = \
	$(built_test_data) \
	$(built_test_scripts) \
	$(NULL)

testsdir = ${datadir}/telepathy-salut-1-tests
twistedtestsdir = ${testsdir}/twisted

if ENABLE_INSTALLED_TESTS
nobase_dist_twistedtests_DATA = ${dist_test_data}
twistedtests_DATA = ${built_test_data}
twistedtests_SCRIPTS = ${built_test_scripts}
endif

run-test.sh: run-test.sh.in Makefile
	$(AM_V_GEN)sed \
			-e 's![@]twistedtestsdir[@]!${twistedtestsdir}!' \
			-e 's![@]TEST_PYTHON[@]!$(TEST_PYTHON)!' \
			< $< > $@.tmp && \
		chmod +x $@.tmp && \
		mv $@.tmp $@

dist_test_data = \
	$(TWISTED_AVAHI_TESTS) \
	$(TWISTED_BASIC_TESTS) \
	avahi/file-transfer/file_transfer_helper.py \
	avahi/tubes/tubetestutil.py \
	avahimock.py \
	avahitest.py \
	constants.py \
	ipv6.py
	ns.py \
	saluttest.py \
	servicetest.py \
	trivialstream.py \
	xmppstream.py \
	$(NULL)

EXTRA_DIST = \
	$(dist_test_data) \
	run-test.sh.in \
	$(NULL)

built_test_data = \
	config.py \
	twisted-tests.list \
	$(NULL)

built_test_scripts = \
	run-test.sh \
	$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES) \
	salut-[1-9]*.log \
	*.pyc \
	*/*.pyc \
	$(NULL)

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools
