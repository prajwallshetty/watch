# Host-side tests for SafeFall AI pure-logic modules (no hardware needed).
#   make        build and run
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-parameter
SRC_DIR  := ../../src
INCLUDES := -Istubs

SOURCES := host_tests.cpp \
           $(SRC_DIR)/fall/FallDetector.cpp \
           $(SRC_DIR)/fall/FallStateMachine.cpp \
           $(SRC_DIR)/services/EventManager.cpp \
           $(SRC_DIR)/ble/BLEProtocol.cpp

run: host_tests
	./host_tests

host_tests: $(SOURCES) $(wildcard stubs/*.h stubs/freertos/*.h)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) -o $@ -lm

clean:
	rm -f host_tests

.PHONY: run clean
