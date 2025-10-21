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

struct Harness {
    Bus bus;
    CPU cpu;

    explicit Harness(std::initializer_list<uint8_t> program, uint16_t origin = 0x8000) {
        cpu.attach_bus(&bus);
        bus.attach_cpu(&cpu);
        std::vector<uint8_t> bytes(program);
        bus.load_program(origin, bytes);
        cpu.reset();
    }

    void step(std::size_t count = 1) {
        for (std::size_t index = 0; index < count; ++index) {
            cpu.step();
        }
    }
};

inline void run_until(Harness& harness, uint64_t max_cycles) {
    const uint64_t start_cycles = harness.cpu.cycles();
    while ((harness.cpu.cycles() - start_cycles) < max_cycles) {
        harness.cpu.step();
    }
}

}  // namespace emulator::testing
