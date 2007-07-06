COVERAGE_DIR=coverage

if HAVE_LCOV
# run lcov from scratch
lcov:
	$(MAKE) lcov-run
	$(MAKE) lcov-report

else
lcov:
	@echo "lcov not found or lacking --compat-libtool support"
	@exit 1
endif

# reset run coverage tests
lcov-run:
	@-rm -rf $(COVERAGE_DIR)
	@-find . -name "*.gcda" -exec rm {} \;
	-$(MAKE) check

# generate report based on current coverage data
lcov-report:
	@mkdir -p $(COVERAGE_DIR)
	@lcov --quiet --compat-libtool --directory . --capture --output-file $(COVERAGE_DIR)/lcov.info
	@lcov --quiet --compat-libtool -l $(COVERAGE_DIR)/lcov.info | grep -v "`cd $(top_srcdir) && pwd`" | cut -d: -f1 > $(COVERAGE_DIR)/remove
	@lcov --quiet --compat-libtool -l $(COVERAGE_DIR)/lcov.info | grep "tests/" | cut -d: -f1 >> $(COVERAGE_DIR)/remove
	@lcov --quiet --compat-libtool -r $(COVERAGE_DIR)/lcov.info `cat $(COVERAGE_DIR)/remove` > $(COVERAGE_DIR)/lcov.cleaned.info
	@rm $(COVERAGE_DIR)/remove
	@mv $(COVERAGE_DIR)/lcov.cleaned.info $(COVERAGE_DIR)/lcov.info
	@echo =================================================
	@genhtml -t "$(PACKAGE_STRING)" -o $(COVERAGE_DIR) $(COVERAGE_DIR)/lcov.info |grep 'Overall'
	@echo =================================================
