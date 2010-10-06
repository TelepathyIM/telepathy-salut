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
	@lcov --output-file $(COVERAGE_DIR)/lcov.info.clean --remove $(COVERAGE_DIR)/lcov.info '*/_gen/*' '/usr/*'
	@echo =================================================
	@genhtml -t "$(PACKAGE_STRING)" -o $(COVERAGE_DIR) $(COVERAGE_DIR)/lcov.info.clean
	@echo file://@abs_top_builddir@/$(COVERAGE_DIR)/index.html
	@echo =================================================
