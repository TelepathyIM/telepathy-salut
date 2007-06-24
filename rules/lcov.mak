COVERAGE_DIR=coverage

# run lcov from scratch, always
lcov-reset:
	make lcov-run
	make lcov-report

# run lcov from scratch if the dir is not there
lcov:
	make lcov-reset

# reset run coverage tests
lcov-run:
	@-rm -rf $(COVERAGE_DIR)
	@-find . -name "*.gcda" -exec rm {} \;
	-make check

# generate report based on current coverage data
lcov-report:
	mkdir $(COVERAGE_DIR)
	$(LCOV_PATH) --compat-libtool --directory . --capture --output-file $(COVERAGE_DIR)/lcov.info
	$(LCOV_PATH) --compat-libtool -l $(COVERAGE_DIR)/lcov.info | grep -v "`cd $(top_srcdir) && pwd`" | cut -d: -f1 > $(COVERAGE_DIR)/remove
	$(LCOV_PATH) --compat-libtool -l $(COVERAGE_DIR)/lcov.info | grep "tests/" | cut -d: -f1 >> $(COVERAGE_DIR)/remove
	$(LCOV_PATH) --compat-libtool -r $(COVERAGE_DIR)/lcov.info `cat $(COVERAGE_DIR)/remove` > $(COVERAGE_DIR)/lcov.cleaned.info
	rm $(COVERAGE_DIR)/remove
	mv $(COVERAGE_DIR)/lcov.cleaned.info $(COVERAGE_DIR)/lcov.info
	genhtml -t "$(PACKAGE_STRING)" -o $(COVERAGE_DIR) $(COVERAGE_DIR)/lcov.info
