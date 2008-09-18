TWISTED_TESTS =

TWISTED_BASIC_TESTS =

TWISTED_AVAHI_TESTS = \
	avahi/test-register.py \
	avahi/test-aliases.py \
	avahi/test-text-channel.py

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
	servicetest.py

CLEANFILES = salut-[1-9]*.log *.pyc */*.pyc

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools
