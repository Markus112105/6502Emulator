#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace emulator {

class CPU;

// Bus mediates all memory operations between the CPU and the emulated memory
// map. The current implementation keeps the design intentionally simple: it
// exposes byte-addressable read/write operations over a fixed 64 KiB address
// space so functional correctness work can proceed before peripheral devices
// are introduced.
class Bus {
public:
    static constexpr std::size_t kAddressSpaceSize = 0x10000;

    Bus();

    void attach_cpu(CPU* cpu);

    uint8_t read(uint16_t address) const;
    uint16_t read16(uint16_t address) const;
    void write(uint16_t address, uint8_t value);

    void load_memory(const std::array<uint8_t, kAddressSpaceSize>& image);
    void load_program(uint16_t start_address, std::span<const uint8_t> program_bytes);

private:
    std::array<uint8_t, kAddressSpaceSize> memory_{};
    CPU* cpu_ = nullptr;
};

}  // namespace emulator
