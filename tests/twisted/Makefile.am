TWISTED_TESTS =

TWISTED_BASIC_TESTS = \
	cm/protocol.py \
	sidecars.py \
	$(NULL)

TWISTED_AVAHI_TESTS = \
	caps_helper.py \
	avahi/register.py \
	avahi/aliases.py \
	avahi/request-im.py \
	avahi/muc-invite.py \
	avahi/caps-file-transfer.py \
	avahi/close-local-pending-room.py \
	avahi/tubes/disabled-1-1-tubes.py \
	avahi/file-transfer/send-file-and-cancel-immediately.py \
	avahi/file-transfer/send-file-and-disconnect.py \
	avahi/file-transfer/send-file-declined.py \
	avahi/file-transfer/send-file-item-not-found.py \
	avahi/file-transfer/send-file-ipv6.py \
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
	avahi/file-transfer/receive-file-not-found.py \
	avahi/file-transfer/metadata.py \
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

TESTS =

TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/tests/twisted:@abs_top_builddir@/tests/twisted:$(PYTHONPATH)

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

check-twisted:
	$(MAKE) -C tools
	rm -f tools/core
	rm -f tools/salut-testing.log
	if ! test -n "$$SALUT_TEST_REAL_AVAHI"; then \
		also_for_system="--also-for-system"; \
	fi; \
	sh $(srcdir)/tools/with-session-bus.sh $$also_for_system --config-file=tools/tmp-session-bus.conf -- $(MAKE) check-TESTS \
		TESTS="$(TWISTED_TESTS)" \
		TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) $(TEST_PYTHON)"
	@if test -e tools/core; then\
		echo "Core dump exists: tools/core";\
		exit 1;\
	fi

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

BUILT_SOURCES = config.py

EXTRA_DIST = \
	$(TWISTED_AVAHI_TESTS) \
	$(TWISTED_AVAHI_OLPC_TESTS) \
	$(TWISTED_BASIC_TESTS) \
	constants.py \
	saluttest.py \
	servicetest.py \
        trivialstream.py \
	avahitest.py \
	avahimock.py \
        config.py \
	ns.py \
	avahi/file-transfer/file_transfer_helper.py \
	avahi/tubes/tubetestutil.py

CLEANFILES = salut-[1-9]*.log *.pyc */*.pyc config.py

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools
