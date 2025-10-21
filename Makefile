CXX := g++
CXXFLAGS := -std=c++20 -O2 -Isrc -Itests

SRC := src/main.cpp src/cpu.cpp src/bus.cpp
TESTS := tests/cpu_tests.cpp tests/integration_timing_tests.cpp

.PHONY: all emulator tests clean
all: emulator

emulator: $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

cpu_tests: $(SRC) $(TESTS)
	$(CXX) $(CXXFLAGS) $(SRC) $(TESTS) -o $@

clean:
	rm -f emulator cpu_tests

