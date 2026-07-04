CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic -Iinclude -O2
LDFLAGS = -lm

# Override for debug
DEBUG ?= 0
ifeq ($(DEBUG),1)
CFLAGS += -g -DDEBUG
endif

# Binaries
BINS    = plato_engine plato_server
TESTS   = test_engine test_protocol test_history
EXAMPLES = minimal alarm_demo multi_sensor symmetry_demo client

.PHONY: all test clean examples

all: $(BINS)

# ---- main binaries ----

plato_engine: src/main.c src/sensors_dummy.c include/plato_engine.h
	$(CC) $(CFLAGS) -o $@ src/main.c src/sensors_dummy.c $(LDFLAGS)

plato_server: src/server.c src/sensors_dummy.c include/plato_engine.h
	$(CC) $(CFLAGS) -o $@ src/server.c src/sensors_dummy.c $(LDFLAGS)

# ---- tests ----

test_engine: tests/test_engine.c include/plato_engine.h
	$(CC) $(CFLAGS) -o $@ tests/test_engine.c $(LDFLAGS)

test_protocol: tests/test_protocol.c include/plato_engine.h
	$(CC) $(CFLAGS) -o $@ tests/test_protocol.c $(LDFLAGS)

test_history: tests/test_history.c include/plato_engine.h
	$(CC) $(CFLAGS) -o $@ tests/test_history.c $(LDFLAGS)

test: test_engine test_protocol test_history
	@echo ""
	@echo "Running tests..."
	@echo ""
	@./test_engine
	@echo ""
	@./test_protocol
	@echo ""
	@./test_history
	@echo ""
	@echo "All tests complete."

# ---- examples ----

examples: $(EXAMPLES)

minimal: examples/minimal.c include/plato_engine.h
	$(CC) $(CFLAGS) -o $@ examples/minimal.c $(LDFLAGS)

alarm_demo: examples/alarm_demo.c include/plato_engine.h
	$(CC) $(CFLAGS) -o $@ examples/alarm_demo.c $(LDFLAGS)

multi_sensor: examples/multi_sensor.c include/plato_engine.h
	$(CC) $(CFLAGS) -o $@ examples/multi_sensor.c $(LDFLAGS)

symmetry_demo: examples/symmetry_demo.c include/plato_engine.h
	$(CC) $(CFLAGS) -o $@ examples/symmetry_demo.c $(LDFLAGS)

client: examples/client.c include/plato_engine.h
	$(CC) $(CFLAGS) -o $@ examples/client.c $(LDFLAGS)

# ---- clean ----

clean:
	rm -f $(BINS) $(TESTS) $(EXAMPLES)
