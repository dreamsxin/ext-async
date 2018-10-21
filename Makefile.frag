test-coverage:
	CCACHE_DISABLE=1 EXTRA_CFLAGS="-fprofile-arcs -ftest-coverage" $(MAKE) clean test

test-coverage-lcov: test-coverage
	lcov -c --directory $(top_srcdir)/.libs --output-file $(top_srcdir)/coverage.info

test-coverage-html: test-coverage-lcov
	genhtml $(top_srcdir)/coverage.info --output-directory=$(top_srcdir)/html

thirdparty/lib/libuv.a:
	set -e; \
	DIR=$$(readlink -f ./thirdparty); \
	if [[ -f "$$DIR/lib/libuv.a" ]]; then exit 0; fi; \
	TMP=$$(mktemp -d); \
	cp -r ./thirdparty/libuv $$TMP/; \
	pushd $$TMP/libuv; \
	./autogen.sh; \
	./configure --prefix=$$TMP/build CFLAGS="$$(CFLAGS) -fPIC -DPIC -g -O2"; \
	make install; \
	popd; \
	cp $$TMP/build/lib/libuv.a $$DIR/lib/; \
	rm -rf $$TMP;
