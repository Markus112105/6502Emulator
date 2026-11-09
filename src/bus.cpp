#include "bus.hpp"

#include <algorithm>

#include "cpu.hpp"

namespace emulator {

Bus::Bus() = default;

void Bus::attach_cpu(CPU* cpu) {
    cpu_ = cpu;
}

uint8_t Bus::read(uint16_t address) const {
    // Every 16-bit address is valid: the whole 64 KiB space is backed by RAM.
    return memory_[address];
}

uint16_t Bus::read16(uint16_t address) const {
    // The 6502 wraps the high byte of indirect reads on page boundaries, so we
    // mirror that behaviour here to keep instruction emulation faithful.
    const uint8_t low = read(address);
    const uint8_t high = read(static_cast<uint16_t>((address & 0xFF00) | ((address + 1) & 0x00FF)));
    return static_cast<uint16_t>(static_cast<uint16_t>(high) << 8 | low);
}

void Bus::write(uint16_t address, uint8_t value) {
    memory_[address] = value;
}

void Bus::load_memory(const std::array<uint8_t, kAddressSpaceSize>& image) {
    std::copy(image.begin(), image.end(), memory_.begin());
}

void Bus::load_program(uint16_t start_address, std::span<const uint8_t> program_bytes) {
    // Copy the program into memory, wrapping at the top of the address space.
    for (std::size_t index = 0; index < program_bytes.size(); ++index) {
        write(static_cast<uint16_t>(start_address + index), program_bytes[index]);
    }

    // Mirror how ROM loaders prime the reset vector so the CPU starts executing
    // the freshly loaded program once reset is asserted.
    write(0xFFFC, static_cast<uint8_t>(start_address & 0x00FF));
    write(0xFFFD, static_cast<uint8_t>((start_address >> 8) & 0x00FF));
}

}  // namespace emulator
