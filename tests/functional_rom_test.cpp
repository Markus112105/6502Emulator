// Runs Klaus Dormann's 6502 functional test ROM against the CPU core.
//
// The ROM is a full 64 KiB memory image that exercises every documented opcode
// and addressing mode, including decimal-mode ADC/SBC. It reports its result
// through the program counter: every failed check jumps to itself forever, and
// so does the final "all tests passed" location. The runner therefore steps the
// CPU until the program counter stops moving, then compares that trap address
// with the known success address.
//
// The ROM is not vendored (it is GPL-licensed); `make functional` downloads it.

#include <array>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "bus.hpp"
#include "cpu.hpp"

namespace {

constexpr uint16_t kEntryPoint = 0x0400;
constexpr uint16_t kSuccessTrap = 0x3469;
// The suite needs roughly 96 million cycles; anything far beyond that means
// the CPU is wandering rather than converging on a trap.
constexpr uint64_t kCycleBudget = 500'000'000;

std::string hex16(uint16_t value) {
    std::ostringstream stream;
    stream << '$' << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << value;
    return stream.str();
}

}  // namespace

int main(int argc, char* argv[]) {
    using emulator::Bus;
    using emulator::CPU;

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <6502_functional_test.bin>\n";
        return 2;
    }

    auto image = std::make_unique<std::array<uint8_t, Bus::kAddressSpaceSize>>();
    std::ifstream file(argv[1], std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(image->data()), static_cast<std::streamsize>(image->size()))) {
        std::cerr << "Expected a 64 KiB memory image at " << argv[1] << '\n';
        return 2;
    }

    Bus bus;
    CPU cpu;
    cpu.attach_bus(&bus);
    bus.attach_cpu(&cpu);
    bus.load_memory(*image);

    // The image is built to be entered at $0400 rather than through its own
    // reset vector, so point the vector there before resetting.
    bus.write(0xFFFC, static_cast<uint8_t>(kEntryPoint & 0xFF));
    bus.write(0xFFFD, static_cast<uint8_t>(kEntryPoint >> 8));
    cpu.reset();

    uint64_t instructions = 0;
    uint16_t trap = 0;
    try {
        while (true) {
            const uint16_t pc_before = cpu.registers().program_counter;
            cpu.step();
            ++instructions;
            if (cpu.registers().program_counter == pc_before) {
                trap = pc_before;
                break;
            }
            if (cpu.cycles() > kCycleBudget) {
                std::cerr << "FAIL: no trap reached within " << kCycleBudget << " cycles (PC="
                          << hex16(cpu.registers().program_counter) << ")\n";
                return 1;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " at PC=" << hex16(cpu.registers().program_counter) << '\n';
        return 1;
    }

    std::cout << "Trapped at " << hex16(trap) << " after " << instructions << " instructions, "
              << cpu.cycles() << " cycles\n";

    if (trap != kSuccessTrap) {
        std::cerr << "FAIL: expected the success trap at " << hex16(kSuccessTrap)
                  << ". Look up " << hex16(trap) << " in 6502_functional_test.lst to see which check failed.\n";
        return 1;
    }

    std::cout << "PASS: Klaus Dormann 6502 functional test\n";
    return 0;
}
