#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace emulator {

// Bus is the 6502's 64 KiB address space. The CPU reads and writes memory only
// through this class, so memory-mapped devices could later be added here
// without changing the CPU. For now every address is plain RAM.
class Bus {
public:
    static constexpr std::size_t kAddressSpaceSize = 0x10000;

    uint8_t read(uint16_t address) const;
    void write(uint16_t address, uint8_t value);

    // Replaces the whole address space with a 64 KiB image.
    void load_memory(const std::array<uint8_t, kAddressSpaceSize>& image);

    // Copies a program to `start_address` and points the reset vector at it,
    // so the CPU begins executing the program after reset().
    void load_program(uint16_t start_address, std::span<const uint8_t> program_bytes);

private:
    std::array<uint8_t, kAddressSpaceSize> memory_{};
};

}  // namespace emulator
