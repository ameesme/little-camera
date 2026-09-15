# Host unit tests for the Arduino-free firmware headers (see test_main.cpp).
#
#   make test

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter

HEADERS = ../../src/chirp.h ../../src/pbm_header.h ../../src/crc32.h ../../src/sha256.h ../../src/shortcode.h ../../src/sync_protocol.h

hosttest: test_main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) test_main.cpp -o hosttest

test: hosttest
	./hosttest

clean:
	rm -rf hosttest hosttest.dSYM

.PHONY: test clean

# Syntax-only compile of the target sources that the preview tool does not
# build (storage, console, identity, main). Arduino/ESP-IDF headers are stubbed
# under stubs/; this catches typos and API mismatches without PlatformIO. It
# is not a substitute for a real build.
CHECK_SRCS = ../../src/audio.cpp ../../src/storage.cpp ../../src/console.cpp ../../src/identity.cpp ../../src/sync.cpp ../../src/main.cpp

check:
	@for f in $(CHECK_SRCS); do \
	  echo "check $$f"; \
	  $(CXX) $(CXXFLAGS) -fsyntax-only -I stubs -I ../preview/shim -I ../../src $$f || exit 1; \
	done
	@echo "syntax ok"

.PHONY: check
