# run lcov from scratch, always
lcov-reset:
	make lcov-run
	make lcov-report

# run lcov from scratch if the dir is not there
lcov:
	make lcov-reset

# reset run coverage tests
lcov-run:
	@-rm -rf lcov
	@-find . -name "*.gcda" -exec rm {} \;
	-make check

# generate report based on current coverage data
lcov-report:
	mkdir lcov
	$(LCOV_PATH) --compat-libtool --directory . --capture --output-file lcov/lcov.info
	$(LCOV_PATH) --compat-libtool -l lcov/lcov.info | grep -v "`cd $(top_srcdir) && pwd`" | cut -d: -f1 > lcov/remove
	$(LCOV_PATH) --compat-libtool -l lcov/lcov.info | grep "tests/" | cut -d: -f1 >> lcov/remove
	$(LCOV_PATH) --compat-libtool -r lcov/lcov.info `cat lcov/remove` > lcov/lcov.cleaned.info
	rm lcov/remove
	mv lcov/lcov.cleaned.info lcov/lcov.info
	genhtml -t "$(PACKAGE_STRING)" -o lcov lcov/lcov.info
