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
	avahi/tubes/disabled-1-1-tubes.py \
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
	avahi/caps-tubes.py \
	avahi/text-channel.py \
	avahi/ichat-composing.py \
	avahi/ichat-incoming-msg.py \
	avahi/file-transfer/ichat-receive-directory.py \
	avahi/file-transfer/ichat-receive-file.py \
	avahi/file-transfer/ichat-send-file.py \
	avahi/file-transfer/ichat-send-file-declined.py  \
	avahi/tubes/request-invalid-dbus-tube.py \
	avahi/request-muc.py \
	avahi/tubes/request-muc-tubes.py \
	avahi/roomlist.py \
	avahi/set-presence.py \
	avahi/tubes/offer-private-stream-tube.py \
	avahi/tubes/two-muc-stream-tubes.py \
	avahi/tubes/two-private-stream-tubes.py \
	avahi/tubes/tube-close.py \
	avahi/tubes/tubes-to-nonexistant-ids.py \
	avahi/tubes/two-muc-dbus-tubes.py

TWISTED_AVAHI_OLPC_TESTS = \
	avahi/olpc-activity-announcements.py

if WANT_TWISTED_TESTS
  TWISTED_TESTS += $(TWISTED_BASIC_TESTS)
endif

if ENABLE_OLPC
  TWISTED_AVAHI_TESTS += $(TWISTED_AVAHI_OLPC_TESTS)
endif

if WANT_TWISTED_AVAHI_TESTS
  TWISTED_TESTS += $(TWISTED_AVAHI_TESTS)
endif

check-local: check-coding-style check-twisted

CHECK_TWISTED_SLEEP=0

check-twisted: $(BUILT_SOURCES)
	$(MAKE) -C tools
	if test "x$(CHECK_TWISTED_SLEEP)" = x0; then \
		salut_test_sleep= ; \
	else \
		salut_test_sleep=--sleep=$(CHECK_TWISTED_SLEEP); \
	fi; \
	SALUT_TEST_UNINSTALLED=1 \
	  SALUT_ABS_TOP_SRCDIR=@abs_top_srcdir@ \
	  SALUT_ABS_TOP_BUILDDIR=@abs_top_builddir@ \
	  SALUT_TEST_SLEEP=$$salut_test_sleep \
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

salut-twisted-tests.list: Makefile
	$(AM_V_GEN)echo $(TWISTED_TESTS) > $@

BUILT_SOURCES = \
	config.py \
	salut-twisted-tests.list \
	run-test.sh \
	$(NULL)

# We don't really use saluttestsdir yet - we only support uninstalled testing
# so far - but I'm substituting it to keep the script more similar to Gabble's.
# ${pkglibexecdir}/tests is what GNOME's InstalledTests goal recommends.
run-test.sh: run-test.sh.in Makefile
	$(AM_V_GEN)sed \
			-e 's![@]saluttestsdir[@]!${pkglibexecdir}/tests!' \
			-e 's![@]TEST_PYTHON[@]!$(TEST_PYTHON)!' \
			< $< > $@.tmp && \
		chmod +x $@.tmp && \
		mv $@.tmp $@

EXTRA_DIST = \
	$(TWISTED_AVAHI_TESTS) \
	$(TWISTED_AVAHI_OLPC_TESTS) \
	$(TWISTED_BASIC_TESTS) \
	constants.py \
	run-test.sh.in \
	saluttest.py \
	servicetest.py \
        trivialstream.py \
	avahitest.py \
	avahimock.py \
        config.py \
	ns.py \
	avahi/file-transfer/file_transfer_helper.py \
	avahi/tubes/tubetestutil.py \
	xmppstream.py \
	ipv6.py

CLEANFILES = \
	$(BUILT_SOURCES) \
	salut-[1-9]*.log \
	*.pyc \
	*/*.pyc \
	$(NULL)

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools
