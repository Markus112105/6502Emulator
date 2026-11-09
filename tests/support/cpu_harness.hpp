#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "bus.hpp"
#include "cpu.hpp"

namespace emulator::testing {

constexpr uint8_t kCarryFlag = static_cast<uint8_t>(CPU::StatusFlag::Carry);
constexpr uint8_t kZeroFlag = static_cast<uint8_t>(CPU::StatusFlag::Zero);
constexpr uint8_t kDecimalFlag = static_cast<uint8_t>(CPU::StatusFlag::Decimal);
constexpr uint8_t kOverflowFlag = static_cast<uint8_t>(CPU::StatusFlag::Overflow);
constexpr uint8_t kNegativeFlag = static_cast<uint8_t>(CPU::StatusFlag::Negative);

// Owns a bus and CPU, loads a short machine-code program, and resets the CPU
// so the first step() executes the program's first instruction.
struct Harness {
    Bus bus;
    CPU cpu{bus};

    explicit Harness(std::initializer_list<uint8_t> program, uint16_t origin = 0x8000) {
        bus.load_program(origin, std::vector<uint8_t>(program));
        cpu.reset();
    }

    void step(std::size_t count = 1) {
        for (std::size_t index = 0; index < count; ++index) {
            cpu.step();
        }
    }
};

}  // namespace emulator::testing
