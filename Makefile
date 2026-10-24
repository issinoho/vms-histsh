# Linux build of the pure core and its unit tests. The VMS build is build_vms.com.
CC ?= cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror -Isrc/core
TESTS = build/test_relay

all: test

build/test_relay: tests/unit/test_relay.c src/core/hs_relay.c src/core/hs_relay.h
	@mkdir -p build
	$(CC) $(CFLAGS) -D_GNU_SOURCE -o $@ tests/unit/test_relay.c src/core/hs_relay.c

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -rf build
.PHONY: all test clean
