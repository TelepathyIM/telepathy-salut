TWISTED_TESTS =

TWISTED_BASIC_TESTS =

TWISTED_AVAHI_TESTS = \
	avahi/caps_helper.py \
	avahi/test-register.py \
	avahi/test-aliases.py \
	avahi/test-request-im.py \
	avahi/test-muc-invite.py \
	avahi/test-caps-file-transfer.py \
	avahi/test-close-local-pending-room.py \
	avahi/tubes/test-disabled-1-1-tubes.py \
	avahi/file-transfer/test-send-file-and-cancel-immediately.py \
	avahi/file-transfer/test-send-file-and-disconnect.py \
	avahi/file-transfer/test-send-file-declined.py \
	avahi/file-transfer/test-send-file-item-not-found.py \
	avahi/file-transfer/test-send-file-ipv6.py \
	avahi/file-transfer/test-send-file-provide-immediately.py \
	avahi/file-transfer/test-send-file-to-unknown-contact.py \
	avahi/file-transfer/test-send-file-wait-to-provide.py \
	avahi/file-transfer/test-receive-and-send-file.py \
	avahi/file-transfer/test-receive-file.py \
	avahi/file-transfer/test-receive-file-and-disconnect.py \
	avahi/file-transfer/test-receive-file-and-sender-disconnect-while-pending.py \
	avahi/file-transfer/test-receive-file-and-sender-disconnect-while-transfering.py \
	avahi/file-transfer/test-receive-file-and-xmpp-disconnect.py \
	avahi/file-transfer/test-receive-file-cancelled-immediately.py \
	avahi/file-transfer/test-receive-file-decline.py \
	avahi/file-transfer/test-receive-file-ipv6.py \
	avahi/file-transfer/test-receive-file-not-found.py \
	avahi/test-caps-self.py \
	avahi/test-caps-tubes.py \
	avahi/test-text-channel.py \
	avahi/test-ichat-composing.py \
	avahi/test-ichat-incoming-msg.py \
	avahi/file-transfer/test-ichat-receive-directory.py \
	avahi/file-transfer/test-ichat-receive-file.py \
	avahi/file-transfer/test-ichat-send-file.py \
	avahi/file-transfer/test-ichat-send-file-declined.py  \
	avahi/tubes/test-request-invalid-dbus-tube.py \
	avahi/test-request-muc.py \
	avahi/tubes/test-request-muc-tubes.py \
        avahi/test-roomlist.py \
	avahi/test-set-presence.py \
	avahi/tubes/test-offer-private-stream-tube.py \
	avahi/tubes/test-two-muc-stream-tubes.py \
	avahi/tubes/test-two-private-stream-tubes.py \
	avahi/tubes/test-tube-close.py \
	avahi/tubes/tubes-to-nonexistant-ids.py \
	avahi/tubes/two-muc-dbus-tubes.py

TWISTED_AVAHI_OLPC_TESTS = \
	avahi/test-olpc-activity-announcements.py

TESTS =

TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/tests/twisted:@abs_top_builddir@/tests/twisted

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
	sh $(srcdir)/tools/with-session-bus.sh --also-for-system --config-file=tools/tmp-session-bus.conf -- $(MAKE) check-TESTS \
		TESTS="$(TWISTED_TESTS)" \
		TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) $(TEST_PYTHON)"
	@if test -e tools/core; then\
		echo "Core dump exists: tools/core";\
		exit 1;\
	fi

config.py: Makefile
	$(AM_V_GEN)echo "PACKAGE_STRING = \"$(PACKAGE_STRING)\"" > config.py

BUILT_SOURCES = config.py

EXTRA_DIST = \
	$(TWISTED_AVAHI_TESTS) \
	$(TWISTED_BASIC_TESTS) \
	constants.py \
	saluttest.py \
	servicetest.py \
        trivialstream.py \
        config.py \
	ns.py \
	avahi/file-transfer/file_transfer_helper.py \
	avahi/tubes/tubetestutil.py

CLEANFILES = salut-[1-9]*.log *.pyc */*.pyc config.py

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools
