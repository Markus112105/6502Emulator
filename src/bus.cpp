#include "bus.hpp"

namespace emulator {

uint8_t Bus::read(uint16_t address) const {
    return memory_[address];
}

void Bus::write(uint16_t address, uint8_t value) {
    memory_[address] = value;
}

void Bus::load_memory(const std::array<uint8_t, kAddressSpaceSize>& image) {
    memory_ = image;
}

void Bus::load_program(uint16_t start_address, std::span<const uint8_t> program_bytes) {
    uint16_t address = start_address;
    for (const uint8_t byte : program_bytes) {
        write(address++, byte);  // Wraps to $0000 past the top of memory.
    }

    // The reset vector is stored low byte first at $FFFC/$FFFD.
    write(0xFFFC, static_cast<uint8_t>(start_address & 0xFF));
    write(0xFFFD, static_cast<uint8_t>(start_address >> 8));
}

}  // namespace emulator
