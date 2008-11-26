TWISTED_TESTS =

TWISTED_BASIC_TESTS =

TWISTED_AVAHI_TESTS = \
	avahi/test-register.py \
	avahi/test-aliases.py \
	avahi/test-request-im.py \
	avahi/test-send-file-and-cancel-immediately.py \
	avahi/test-send-file-and-disconnect.py \
	avahi/test-send-file-declined.py \
	avahi/test-send-file-item-not-found.py \
	avahi/test-send-file-provide-immediately.py \
	avahi/test-send-file-to-unknown-contact.py \
	avahi/test-send-file-wait-to-provide.py \
	avahi/test-receive-and-send-file.py \
	avahi/test-receive-file.py \
	avahi/test-receive-file-cancelled-immediately.py \
	avahi/test-receive-file-decline.py \
	avahi/test-receive-file-not-found.py \
	avahi/test-text-channel.py \
	avahi/test-ichat-composing.py \
	avahi/test-ichat-incoming-msg.py \
	avahi/test-room-list.py \
	avahi/test-tube.py \
	avahi/test-two-tubes.py \
	avahi/test-tube-close.py

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
	sh $(srcdir)/tools/with-session-bus.sh --config-file=tools/tmp-session-bus.conf -- $(MAKE) check-TESTS \
		TESTS="$(TWISTED_TESTS)" \
		TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) $(TEST_PYTHON)"
	@if test -e tools/core; then\
		echo "Core dump exists: tools/core";\
		exit 1;\
	fi

EXTRA_DIST = \
	$(TWISTED_AVAHI_TESTS) \
	$(TWISTED_BASIC_TESTS) \
	saluttest.py \
	servicetest.py \
        trivialstream.py

CLEANFILES = salut-[1-9]*.log *.pyc */*.pyc

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools
