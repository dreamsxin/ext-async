test-coverage:
	CCACHE_DISABLE=1 EXTRA_CFLAGS="-fprofile-arcs -ftest-coverage" $(MAKE) clean test

test-coverage-lcov: test-coverage
	lcov -c --directory $(top_srcdir)/.libs --output-file $(top_srcdir)/coverage.info

test-coverage-html: test-coverage-lcov
	genhtml $(top_srcdir)/coverage.info --output-directory=$(top_srcdir)/html

thirdparty/lib/libuv.a:
	bash $(top_srcdir)/install-libuv.sh
