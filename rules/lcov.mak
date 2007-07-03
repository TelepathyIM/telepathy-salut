COVERAGE_DIR=coverage

# run lcov from scratch, always
lcov-reset:
	$(MAKE) lcov-run
	$(MAKE) lcov-report

# run lcov from scratch if the dir is not there
lcov:
	$(MAKE) lcov-reset

# reset run coverage tests
lcov-run:
	@-rm -rf $(COVERAGE_DIR)
	@-find . -name "*.gcda" -exec rm {} \;
	-$(MAKE) check

# generate report based on current coverage data
lcov-report:
	@if test -z "$(LCOV_PATH)"; then				\
	     echo "WARNING: the 'lcov' executable was not found";	\
	     echo "Use the \$$LCOV_PATH variable to set its location";	\
	     exit 1;							\
	 fi
	@mkdir -p $(COVERAGE_DIR)
	@$(LCOV_PATH) --quiet --compat-libtool --directory . --capture --output-file $(COVERAGE_DIR)/lcov.info
	@$(LCOV_PATH) --quiet --compat-libtool -l $(COVERAGE_DIR)/lcov.info | grep -v "`cd $(top_srcdir) && pwd`" | cut -d: -f1 > $(COVERAGE_DIR)/remove
	@$(LCOV_PATH) --quiet --compat-libtool -l $(COVERAGE_DIR)/lcov.info | grep "tests/" | cut -d: -f1 >> $(COVERAGE_DIR)/remove
	@$(LCOV_PATH) --quiet --compat-libtool -r $(COVERAGE_DIR)/lcov.info `cat $(COVERAGE_DIR)/remove` > $(COVERAGE_DIR)/lcov.cleaned.info
	@rm $(COVERAGE_DIR)/remove
	@mv $(COVERAGE_DIR)/lcov.cleaned.info $(COVERAGE_DIR)/lcov.info
	@echo =================================================
	@genhtml -t "$(PACKAGE_STRING)" -o $(COVERAGE_DIR) $(COVERAGE_DIR)/lcov.info |grep 'Overall'
	@echo =================================================
