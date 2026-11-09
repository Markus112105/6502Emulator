CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Isrc -Itests

BUILD := build
CORE  := src/cpu.cpp src/bus.cpp
UNIT  := tests/cpu_tests.cpp tests/integration_timing_tests.cpp
HDRS  := $(wildcard src/*.hpp tests/*/*.hpp)

# Klaus Dormann's functional test ROM, pinned to a commit and checksummed.
ROM_COMMIT := 7954e2dbb49c469ea286070bf46cdd71aeb29e4b
ROM_URL    := https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/$(ROM_COMMIT)/bin_files/6502_functional_test.bin
ROM_SHA256 := fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd
ROM        := $(BUILD)/6502_functional_test.bin

.PHONY: all test functional check clean
all: emulator

emulator: src/main.cpp $(CORE) $(HDRS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp $(CORE) -o $@

$(BUILD):
	mkdir -p $@

$(BUILD)/cpu_tests: $(CORE) $(UNIT) $(HDRS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) $(UNIT) -o $@

$(BUILD)/functional_rom_test: $(CORE) tests/functional_rom_test.cpp $(HDRS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) tests/functional_rom_test.cpp -o $@

$(ROM): | $(BUILD)
	curl -fsSL -o $@ $(ROM_URL)
	echo "$(ROM_SHA256)  $@" | shasum -a 256 -c -

# Unit and timing tests.
test: $(BUILD)/cpu_tests
	./$(BUILD)/cpu_tests

# Full-instruction-set validation against the functional test ROM (needs network once).
functional: $(BUILD)/functional_rom_test $(ROM)
	./$(BUILD)/functional_rom_test $(ROM)

check: test functional

clean:
	rm -rf $(BUILD) emulator
