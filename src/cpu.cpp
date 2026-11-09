#include "cpu.hpp"

#include <stdexcept>
#include <string>

#include "bus.hpp"

namespace emulator {

namespace {
constexpr uint16_t kStackBase = 0x0100;
constexpr uint8_t kBreakMask = static_cast<uint8_t>(CPU::StatusFlag::Break);
constexpr uint8_t kUnusedMask = static_cast<uint8_t>(CPU::StatusFlag::Unused);
}  // namespace

CPU::CPU() {
    build_dispatch_table();
}

void CPU::attach_bus(Bus* bus) {
    bus_ = bus;
}

void CPU::reset() {
    // Reset aligns with the 6502 power-on sequence so that software relying on
    // the reset vector and status flag defaults behaves as expected.
    registers_ = Registers{};
    registers_.status = static_cast<uint8_t>((registers_.status & ~kBreakMask) | kUnusedMask);

    if (bus_ != nullptr) {
        const uint8_t low = bus_->read(0xFFFC);
        const uint8_t high = bus_->read(0xFFFD);
        registers_.program_counter = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
    } else {
        registers_.program_counter = 0x0000;
    }

    cycles_ = 0;
}

void CPU::step() {
    if (bus_ == nullptr) {
        return;
    }

    const uint8_t opcode = fetch_byte();
    execute_instruction(opcode);
}

uint8_t CPU::fetch_byte() {
    const uint8_t value = bus_->read(registers_.program_counter);
    registers_.program_counter = static_cast<uint16_t>(registers_.program_counter + 1);
    return value;
}

uint16_t CPU::fetch_word() {
    const uint16_t low = fetch_byte();
    const uint16_t high = fetch_byte();
    return static_cast<uint16_t>((high << 8) | low);
}

uint8_t CPU::read(uint16_t address) const {
    return bus_->read(address);
}

void CPU::write(uint16_t address, uint8_t value) {
    bus_->write(address, value);
}

void CPU::push(uint8_t value) {
    const uint16_t address = static_cast<uint16_t>(kStackBase | registers_.stack_pointer);
    write(address, value);
    registers_.stack_pointer = static_cast<uint8_t>(registers_.stack_pointer - 1);
}

void CPU::push16(uint16_t value) {
    push(static_cast<uint8_t>((value >> 8) & 0x00FF));
    push(static_cast<uint8_t>(value & 0x00FF));
}

uint8_t CPU::pull() {
    registers_.stack_pointer = static_cast<uint8_t>(registers_.stack_pointer + 1);
    const uint16_t address = static_cast<uint16_t>(kStackBase | registers_.stack_pointer);
    return read(address);
}

uint16_t CPU::pull16() {
    const uint16_t low = pull();
    const uint16_t high = pull();
    return static_cast<uint16_t>((high << 8) | low);
}

void CPU::consume_operand(AddressingMode mode) {
    switch (mode) {
        case AddressingMode::Accumulator:
        case AddressingMode::Implied:
            return;
        case AddressingMode::Immediate:
        case AddressingMode::Relative:
            fetch_byte();
            return;
        default: {
            bool page_crossed = false;
            compute_address(mode, page_crossed);
            page_crossed_ = page_crossed_ || page_crossed;
            return;
        }
    }
}

void CPU::adc(uint8_t operand) {
    const uint8_t accumulator = registers_.accumulator;
    const uint16_t carry_in = flag(StatusFlag::Carry) ? 1 : 0;
    const uint16_t binary_sum = static_cast<uint16_t>(accumulator) + operand + carry_in;
    uint16_t adjusted_sum = binary_sum;
    const bool decimal_mode = flag(StatusFlag::Decimal);

    if (decimal_mode) {
        if (((accumulator & 0x0F) + (operand & 0x0F) + carry_in) > 0x09) {
            adjusted_sum += 0x06;
        }
        if (adjusted_sum > 0x99) {
            adjusted_sum += 0x60;
        }
    }

    const uint8_t binary_result = static_cast<uint8_t>(binary_sum & 0xFF);
    set_flag(StatusFlag::Overflow,
             (~(accumulator ^ operand) & (accumulator ^ binary_result) & 0x80) != 0);

    set_flag(StatusFlag::Carry, decimal_mode ? (adjusted_sum > 0x99) : (binary_sum > 0xFF));

    const uint8_t result = static_cast<uint8_t>(adjusted_sum & 0xFF);
    registers_.accumulator = result;
    update_zero_and_negative(result);
}

void CPU::sbc(uint8_t operand) {
    const uint8_t accumulator = registers_.accumulator;
    const uint16_t carry_in = flag(StatusFlag::Carry) ? 1 : 0;
    const uint16_t value = static_cast<uint16_t>(operand) ^ 0x00FF;
    const uint16_t binary_sum = static_cast<uint16_t>(accumulator) + value + carry_in;
    const uint8_t binary_result = static_cast<uint8_t>(binary_sum & 0xFF);
    const bool decimal_mode = flag(StatusFlag::Decimal);

    uint8_t result = binary_result;
    if (decimal_mode) {
        int low = static_cast<int>(accumulator & 0x0F) - static_cast<int>(operand & 0x0F) - static_cast<int>(carry_in ? 0 : 1);
        int borrow = 0;
        if (low < 0) {
            low += 10;
            borrow = 1;
        }

        int high = static_cast<int>((accumulator >> 4) & 0x0F) - static_cast<int>((operand >> 4) & 0x0F) - borrow;
        int carry_out = 1;
        if (high < 0) {
            high += 10;
            carry_out = 0;
        }

        result = static_cast<uint8_t>((static_cast<uint8_t>(high) << 4) | static_cast<uint8_t>(low & 0x0F));
        set_flag(StatusFlag::Carry, carry_out != 0);
    } else {
        set_flag(StatusFlag::Carry, binary_sum > 0xFF);
    }

    set_flag(StatusFlag::Overflow,
             ((accumulator ^ binary_result) & (accumulator ^ operand) & 0x80) != 0);

    registers_.accumulator = result;
    update_zero_and_negative(result);
}

bool CPU::flag(StatusFlag flag) const {
    return (registers_.status & static_cast<uint8_t>(flag)) != 0;
}

void CPU::set_flag(StatusFlag flag, bool value) {
    if (value) {
        registers_.status |= static_cast<uint8_t>(flag);
    } else {
        registers_.status &= ~static_cast<uint8_t>(flag);
    }
}

void CPU::update_zero_and_negative(uint8_t value) {
    set_flag(StatusFlag::Zero, value == 0);
    set_flag(StatusFlag::Negative, (value & 0x80) != 0);
}

uint16_t CPU::compute_address(AddressingMode mode, bool& page_crossed) {
    page_crossed = false;

    switch (mode) {
        case AddressingMode::Immediate:
            return registers_.program_counter++;
        case AddressingMode::ZeroPage:
            return static_cast<uint16_t>(fetch_byte());
        case AddressingMode::ZeroPageX: {
            const uint8_t base = fetch_byte();
            return static_cast<uint16_t>(static_cast<uint8_t>(base + registers_.x));
        }
        case AddressingMode::ZeroPageY: {
            const uint8_t base = fetch_byte();
            return static_cast<uint16_t>(static_cast<uint8_t>(base + registers_.y));
        }
        case AddressingMode::Absolute:
            return fetch_word();
        case AddressingMode::AbsoluteX: {
            const uint16_t base = fetch_word();
            const uint16_t address = static_cast<uint16_t>(base + registers_.x);
            page_crossed = (base & 0xFF00) != (address & 0xFF00);
            return address;
        }
        case AddressingMode::AbsoluteY: {
            const uint16_t base = fetch_word();
            const uint16_t address = static_cast<uint16_t>(base + registers_.y);
            page_crossed = (base & 0xFF00) != (address & 0xFF00);
            return address;
        }
        case AddressingMode::Indirect: {
            const uint16_t pointer = fetch_word();
            const uint8_t low = read(pointer);
            const uint8_t high = read(static_cast<uint16_t>((pointer & 0xFF00) | ((pointer + 1) & 0x00FF)));
            return static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
        }
        case AddressingMode::IndexedIndirect: {
            const uint8_t base = static_cast<uint8_t>(fetch_byte() + registers_.x);
            const uint8_t low = read(static_cast<uint16_t>(base));
            const uint8_t high = read(static_cast<uint16_t>((base + 1) & 0x00FF));
            return static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
        }
        case AddressingMode::IndirectIndexed: {
            const uint8_t base = fetch_byte();
            const uint8_t low = read(static_cast<uint16_t>(base));
            const uint8_t high = read(static_cast<uint16_t>((base + 1) & 0x00FF));
            const uint16_t pointer = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
            const uint16_t address = static_cast<uint16_t>(pointer + registers_.y);
            page_crossed = (pointer & 0xFF00) != (address & 0xFF00);
            return address;
        }
        case AddressingMode::Relative: {
            const int8_t offset = static_cast<int8_t>(fetch_byte());
            const uint16_t current = registers_.program_counter;
            const uint16_t target = static_cast<uint16_t>(current + offset);
            page_crossed = (current & 0xFF00) != (target & 0xFF00);
            return target;
        }
        case AddressingMode::Accumulator:
        case AddressingMode::Implied:
            break;
    }

    return 0;
}

uint8_t CPU::read_operand(AddressingMode mode, bool& page_crossed) {
    switch (mode) {
        case AddressingMode::Accumulator:
            page_crossed = false;
            return registers_.accumulator;
        case AddressingMode::Immediate:
            page_crossed = false;
            return fetch_byte();
        case AddressingMode::Implied:
            page_crossed = false;
            return 0;
        default: {
            const uint16_t address = compute_address(mode, page_crossed);
            page_crossed_ = page_crossed_ || page_crossed;
            return read(address);
        }
    }
}

void CPU::build_dispatch_table() {
    // Default every descriptor to an illegal instruction trap so unsupported
    // opcodes are surfaced immediately during development.
    for (auto& descriptor : dispatch_table_) {
        descriptor.handler = nullptr;
        descriptor.mode = AddressingMode::Implied;
        descriptor.cycles = 0;
        descriptor.page_cross_cycle = false;
    }

    auto set = [this](uint8_t opcode, InstructionDescriptor::Handler handler,
                      AddressingMode mode, uint8_t cycles, bool page_cross_extra = false) {
        auto& descriptor = dispatch_table_[opcode];
        descriptor.handler = handler;
        descriptor.mode = mode;
        descriptor.cycles = cycles;
        descriptor.page_cross_cycle = page_cross_extra;
    };

    // --- Load/Store ---
    set(0xA9, &CPU::op_lda, AddressingMode::Immediate, 2);
    set(0xA5, &CPU::op_lda, AddressingMode::ZeroPage, 3);
    set(0xB5, &CPU::op_lda, AddressingMode::ZeroPageX, 4);
    set(0xAD, &CPU::op_lda, AddressingMode::Absolute, 4);
    set(0xBD, &CPU::op_lda, AddressingMode::AbsoluteX, 4, true);
    set(0xB9, &CPU::op_lda, AddressingMode::AbsoluteY, 4, true);
    set(0xA1, &CPU::op_lda, AddressingMode::IndexedIndirect, 6);
    set(0xB1, &CPU::op_lda, AddressingMode::IndirectIndexed, 5, true);

    set(0xA2, &CPU::op_ldx, AddressingMode::Immediate, 2);
    set(0xA6, &CPU::op_ldx, AddressingMode::ZeroPage, 3);
    set(0xB6, &CPU::op_ldx, AddressingMode::ZeroPageY, 4);
    set(0xAE, &CPU::op_ldx, AddressingMode::Absolute, 4);
    set(0xBE, &CPU::op_ldx, AddressingMode::AbsoluteY, 4, true);

    set(0xA0, &CPU::op_ldy, AddressingMode::Immediate, 2);
    set(0xA4, &CPU::op_ldy, AddressingMode::ZeroPage, 3);
    set(0xB4, &CPU::op_ldy, AddressingMode::ZeroPageX, 4);
    set(0xAC, &CPU::op_ldy, AddressingMode::Absolute, 4);
    set(0xBC, &CPU::op_ldy, AddressingMode::AbsoluteX, 4, true);

    set(0x85, &CPU::op_sta, AddressingMode::ZeroPage, 3);
    set(0x95, &CPU::op_sta, AddressingMode::ZeroPageX, 4);
    set(0x8D, &CPU::op_sta, AddressingMode::Absolute, 4);
    set(0x9D, &CPU::op_sta, AddressingMode::AbsoluteX, 5);
    set(0x99, &CPU::op_sta, AddressingMode::AbsoluteY, 5);
    set(0x81, &CPU::op_sta, AddressingMode::IndexedIndirect, 6);
    set(0x91, &CPU::op_sta, AddressingMode::IndirectIndexed, 6);

    set(0x86, &CPU::op_stx, AddressingMode::ZeroPage, 3);
    set(0x96, &CPU::op_stx, AddressingMode::ZeroPageY, 4);
    set(0x8E, &CPU::op_stx, AddressingMode::Absolute, 4);

    set(0x84, &CPU::op_sty, AddressingMode::ZeroPage, 3);
    set(0x94, &CPU::op_sty, AddressingMode::ZeroPageX, 4);
    set(0x8C, &CPU::op_sty, AddressingMode::Absolute, 4);

    // --- Arithmetic / Logic ---
    set(0x69, &CPU::op_adc, AddressingMode::Immediate, 2);
    set(0x65, &CPU::op_adc, AddressingMode::ZeroPage, 3);
    set(0x75, &CPU::op_adc, AddressingMode::ZeroPageX, 4);
    set(0x6D, &CPU::op_adc, AddressingMode::Absolute, 4);
    set(0x7D, &CPU::op_adc, AddressingMode::AbsoluteX, 4, true);
    set(0x79, &CPU::op_adc, AddressingMode::AbsoluteY, 4, true);
    set(0x61, &CPU::op_adc, AddressingMode::IndexedIndirect, 6);
    set(0x71, &CPU::op_adc, AddressingMode::IndirectIndexed, 5, true);

    set(0xE9, &CPU::op_sbc, AddressingMode::Immediate, 2);
    set(0xE5, &CPU::op_sbc, AddressingMode::ZeroPage, 3);
    set(0xF5, &CPU::op_sbc, AddressingMode::ZeroPageX, 4);
    set(0xED, &CPU::op_sbc, AddressingMode::Absolute, 4);
    set(0xFD, &CPU::op_sbc, AddressingMode::AbsoluteX, 4, true);
    set(0xF9, &CPU::op_sbc, AddressingMode::AbsoluteY, 4, true);
    set(0xE1, &CPU::op_sbc, AddressingMode::IndexedIndirect, 6);
    set(0xF1, &CPU::op_sbc, AddressingMode::IndirectIndexed, 5, true);

    set(0x29, &CPU::op_and, AddressingMode::Immediate, 2);
    set(0x25, &CPU::op_and, AddressingMode::ZeroPage, 3);
    set(0x35, &CPU::op_and, AddressingMode::ZeroPageX, 4);
    set(0x2D, &CPU::op_and, AddressingMode::Absolute, 4);
    set(0x3D, &CPU::op_and, AddressingMode::AbsoluteX, 4, true);
    set(0x39, &CPU::op_and, AddressingMode::AbsoluteY, 4, true);
    set(0x21, &CPU::op_and, AddressingMode::IndexedIndirect, 6);
    set(0x31, &CPU::op_and, AddressingMode::IndirectIndexed, 5, true);

    set(0x09, &CPU::op_ora, AddressingMode::Immediate, 2);
    set(0x05, &CPU::op_ora, AddressingMode::ZeroPage, 3);
    set(0x15, &CPU::op_ora, AddressingMode::ZeroPageX, 4);
    set(0x0D, &CPU::op_ora, AddressingMode::Absolute, 4);
    set(0x1D, &CPU::op_ora, AddressingMode::AbsoluteX, 4, true);
    set(0x19, &CPU::op_ora, AddressingMode::AbsoluteY, 4, true);
    set(0x01, &CPU::op_ora, AddressingMode::IndexedIndirect, 6);
    set(0x11, &CPU::op_ora, AddressingMode::IndirectIndexed, 5, true);

    set(0x49, &CPU::op_eor, AddressingMode::Immediate, 2);
    set(0x45, &CPU::op_eor, AddressingMode::ZeroPage, 3);
    set(0x55, &CPU::op_eor, AddressingMode::ZeroPageX, 4);
    set(0x4D, &CPU::op_eor, AddressingMode::Absolute, 4);
    set(0x5D, &CPU::op_eor, AddressingMode::AbsoluteX, 4, true);
    set(0x59, &CPU::op_eor, AddressingMode::AbsoluteY, 4, true);
    set(0x41, &CPU::op_eor, AddressingMode::IndexedIndirect, 6);
    set(0x51, &CPU::op_eor, AddressingMode::IndirectIndexed, 5, true);

    set(0xC9, &CPU::op_cmp, AddressingMode::Immediate, 2);
    set(0xC5, &CPU::op_cmp, AddressingMode::ZeroPage, 3);
    set(0xD5, &CPU::op_cmp, AddressingMode::ZeroPageX, 4);
    set(0xCD, &CPU::op_cmp, AddressingMode::Absolute, 4);
    set(0xDD, &CPU::op_cmp, AddressingMode::AbsoluteX, 4, true);
    set(0xD9, &CPU::op_cmp, AddressingMode::AbsoluteY, 4, true);
    set(0xC1, &CPU::op_cmp, AddressingMode::IndexedIndirect, 6);
    set(0xD1, &CPU::op_cmp, AddressingMode::IndirectIndexed, 5, true);

    set(0xE0, &CPU::op_cpx, AddressingMode::Immediate, 2);
    set(0xE4, &CPU::op_cpx, AddressingMode::ZeroPage, 3);
    set(0xEC, &CPU::op_cpx, AddressingMode::Absolute, 4);

    set(0xC0, &CPU::op_cpy, AddressingMode::Immediate, 2);
    set(0xC4, &CPU::op_cpy, AddressingMode::ZeroPage, 3);
    set(0xCC, &CPU::op_cpy, AddressingMode::Absolute, 4);

    set(0x24, &CPU::op_bit, AddressingMode::ZeroPage, 3);
    set(0x2C, &CPU::op_bit, AddressingMode::Absolute, 4);

    // --- Increment / Decrement ---
    set(0xE6, &CPU::op_inc, AddressingMode::ZeroPage, 5);
    set(0xF6, &CPU::op_inc, AddressingMode::ZeroPageX, 6);
    set(0xEE, &CPU::op_inc, AddressingMode::Absolute, 6);
    set(0xFE, &CPU::op_inc, AddressingMode::AbsoluteX, 7);

    set(0xC6, &CPU::op_dec, AddressingMode::ZeroPage, 5);
    set(0xD6, &CPU::op_dec, AddressingMode::ZeroPageX, 6);
    set(0xCE, &CPU::op_dec, AddressingMode::Absolute, 6);
    set(0xDE, &CPU::op_dec, AddressingMode::AbsoluteX, 7);

    set(0xE8, &CPU::op_inx, AddressingMode::Implied, 2);
    set(0xC8, &CPU::op_iny, AddressingMode::Implied, 2);
    set(0xCA, &CPU::op_dex, AddressingMode::Implied, 2);
    set(0x88, &CPU::op_dey, AddressingMode::Implied, 2);

    // --- Shift / Rotate ---
    set(0x0A, &CPU::op_asl, AddressingMode::Accumulator, 2);
    set(0x06, &CPU::op_asl, AddressingMode::ZeroPage, 5);
    set(0x16, &CPU::op_asl, AddressingMode::ZeroPageX, 6);
    set(0x0E, &CPU::op_asl, AddressingMode::Absolute, 6);
    set(0x1E, &CPU::op_asl, AddressingMode::AbsoluteX, 7);

    set(0x4A, &CPU::op_lsr, AddressingMode::Accumulator, 2);
    set(0x46, &CPU::op_lsr, AddressingMode::ZeroPage, 5);
    set(0x56, &CPU::op_lsr, AddressingMode::ZeroPageX, 6);
    set(0x4E, &CPU::op_lsr, AddressingMode::Absolute, 6);
    set(0x5E, &CPU::op_lsr, AddressingMode::AbsoluteX, 7);

    set(0x2A, &CPU::op_rol, AddressingMode::Accumulator, 2);
    set(0x26, &CPU::op_rol, AddressingMode::ZeroPage, 5);
    set(0x36, &CPU::op_rol, AddressingMode::ZeroPageX, 6);
    set(0x2E, &CPU::op_rol, AddressingMode::Absolute, 6);
    set(0x3E, &CPU::op_rol, AddressingMode::AbsoluteX, 7);

    set(0x6A, &CPU::op_ror, AddressingMode::Accumulator, 2);
    set(0x66, &CPU::op_ror, AddressingMode::ZeroPage, 5);
    set(0x76, &CPU::op_ror, AddressingMode::ZeroPageX, 6);
    set(0x6E, &CPU::op_ror, AddressingMode::Absolute, 6);
    set(0x7E, &CPU::op_ror, AddressingMode::AbsoluteX, 7);

    // --- Branches ---
    set(0x10, &CPU::op_bpl, AddressingMode::Relative, 2);
    set(0x30, &CPU::op_bmi, AddressingMode::Relative, 2);
    set(0x50, &CPU::op_bvc, AddressingMode::Relative, 2);
    set(0x70, &CPU::op_bvs, AddressingMode::Relative, 2);
    set(0x90, &CPU::op_bcc, AddressingMode::Relative, 2);
    set(0xB0, &CPU::op_bcs, AddressingMode::Relative, 2);
    set(0xD0, &CPU::op_bne, AddressingMode::Relative, 2);
    set(0xF0, &CPU::op_beq, AddressingMode::Relative, 2);

    // --- Transfer / Stack ---
    set(0xAA, &CPU::op_tax, AddressingMode::Implied, 2);
    set(0xA8, &CPU::op_tay, AddressingMode::Implied, 2);
    set(0x8A, &CPU::op_txa, AddressingMode::Implied, 2);
    set(0x98, &CPU::op_tya, AddressingMode::Implied, 2);
    set(0xBA, &CPU::op_tsx, AddressingMode::Implied, 2);
    set(0x9A, &CPU::op_txs, AddressingMode::Implied, 2);

    set(0x48, &CPU::op_pha, AddressingMode::Implied, 3);
    set(0x08, &CPU::op_php, AddressingMode::Implied, 3);
    set(0x68, &CPU::op_pla, AddressingMode::Implied, 4);
    set(0x28, &CPU::op_plp, AddressingMode::Implied, 4);

    // --- Status ---
    set(0x18, &CPU::op_clc, AddressingMode::Implied, 2);
    set(0x38, &CPU::op_sec, AddressingMode::Implied, 2);
    set(0x58, &CPU::op_cli, AddressingMode::Implied, 2);
    set(0x78, &CPU::op_sei, AddressingMode::Implied, 2);
    set(0xB8, &CPU::op_clv, AddressingMode::Implied, 2);
    set(0xD8, &CPU::op_cld, AddressingMode::Implied, 2);
    set(0xF8, &CPU::op_sed, AddressingMode::Implied, 2);

    // --- Control ---
    set(0x00, &CPU::op_brk, AddressingMode::Implied, 7);
    set(0xEA, &CPU::op_nop, AddressingMode::Implied, 2);
    set(0x4C, &CPU::op_jmp, AddressingMode::Absolute, 3);
    set(0x6C, &CPU::op_jmp, AddressingMode::Indirect, 5);
    set(0x20, &CPU::op_jsr, AddressingMode::Absolute, 6);
    set(0x40, &CPU::op_rti, AddressingMode::Implied, 6);
    set(0x60, &CPU::op_rts, AddressingMode::Implied, 6);

    // --- Undocumented operations ---
    for (const uint8_t opcode : {0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72, 0x92, 0xB2, 0xD2, 0xF2}) {
        set(opcode, &CPU::op_jam, AddressingMode::Implied, 0);
    }

    for (const uint8_t opcode : {0x1A, 0x3A, 0x5A, 0x7A, 0xDA, 0xFA}) {
        set(opcode, &CPU::op_nop, AddressingMode::Implied, 2);
    }

    for (const uint8_t opcode : {0x80, 0x82, 0x89, 0xC2, 0xE2}) {
        set(opcode, &CPU::op_nop, AddressingMode::Immediate, 2);
    }

    for (const uint8_t opcode : {0x04, 0x44, 0x64}) {
        set(opcode, &CPU::op_nop, AddressingMode::ZeroPage, 3);
    }

    for (const uint8_t opcode : {0x14, 0x34, 0x54, 0x74, 0xD4, 0xF4}) {
        set(opcode, &CPU::op_nop, AddressingMode::ZeroPageX, 4);
    }

    set(0x0C, &CPU::op_nop, AddressingMode::Absolute, 4);
    for (const uint8_t opcode : {0x1C, 0x3C, 0x5C, 0x7C, 0xDC, 0xFC}) {
        set(opcode, &CPU::op_nop, AddressingMode::AbsoluteX, 4, true);
    }

    for (const uint8_t opcode : {0x0B, 0x2B}) {
        set(opcode, &CPU::op_anc, AddressingMode::Immediate, 2);
    }
    set(0x4B, &CPU::op_alr, AddressingMode::Immediate, 2);
    set(0x6B, &CPU::op_arr, AddressingMode::Immediate, 2);
    set(0x8B, &CPU::op_ane, AddressingMode::Immediate, 2);
    set(0xAB, &CPU::op_lxa, AddressingMode::Immediate, 2);
    set(0xCB, &CPU::op_sbx, AddressingMode::Immediate, 2);
    set(0xEB, &CPU::op_sbc, AddressingMode::Immediate, 2);

    // SLO (ASL then ORA)
    set(0x03, &CPU::op_slo, AddressingMode::IndexedIndirect, 8);
    set(0x07, &CPU::op_slo, AddressingMode::ZeroPage, 5);
    set(0x0F, &CPU::op_slo, AddressingMode::Absolute, 6);
    set(0x13, &CPU::op_slo, AddressingMode::IndirectIndexed, 8);
    set(0x17, &CPU::op_slo, AddressingMode::ZeroPageX, 6);
    set(0x1B, &CPU::op_slo, AddressingMode::AbsoluteY, 7);
    set(0x1F, &CPU::op_slo, AddressingMode::AbsoluteX, 7);

    // RLA (ROL then AND)
    set(0x23, &CPU::op_rla, AddressingMode::IndexedIndirect, 8);
    set(0x27, &CPU::op_rla, AddressingMode::ZeroPage, 5);
    set(0x2F, &CPU::op_rla, AddressingMode::Absolute, 6);
    set(0x33, &CPU::op_rla, AddressingMode::IndirectIndexed, 8);
    set(0x37, &CPU::op_rla, AddressingMode::ZeroPageX, 6);
    set(0x3B, &CPU::op_rla, AddressingMode::AbsoluteY, 7);
    set(0x3F, &CPU::op_rla, AddressingMode::AbsoluteX, 7);

    // SRE (LSR then EOR)
    set(0x43, &CPU::op_sre, AddressingMode::IndexedIndirect, 8);
    set(0x47, &CPU::op_sre, AddressingMode::ZeroPage, 5);
    set(0x4F, &CPU::op_sre, AddressingMode::Absolute, 6);
    set(0x53, &CPU::op_sre, AddressingMode::IndirectIndexed, 8);
    set(0x57, &CPU::op_sre, AddressingMode::ZeroPageX, 6);
    set(0x5B, &CPU::op_sre, AddressingMode::AbsoluteY, 7);
    set(0x5F, &CPU::op_sre, AddressingMode::AbsoluteX, 7);

    // RRA (ROR then ADC)
    set(0x63, &CPU::op_rra, AddressingMode::IndexedIndirect, 8);
    set(0x67, &CPU::op_rra, AddressingMode::ZeroPage, 5);
    set(0x6F, &CPU::op_rra, AddressingMode::Absolute, 6);
    set(0x73, &CPU::op_rra, AddressingMode::IndirectIndexed, 8);
    set(0x77, &CPU::op_rra, AddressingMode::ZeroPageX, 6);
    set(0x7B, &CPU::op_rra, AddressingMode::AbsoluteY, 7);
    set(0x7F, &CPU::op_rra, AddressingMode::AbsoluteX, 7);

    // SAX (store A & X)
    set(0x83, &CPU::op_sax, AddressingMode::IndexedIndirect, 6);
    set(0x87, &CPU::op_sax, AddressingMode::ZeroPage, 3);
    set(0x8F, &CPU::op_sax, AddressingMode::Absolute, 4);
    set(0x97, &CPU::op_sax, AddressingMode::ZeroPageY, 4);

    // SHA / SHS / SHX / SHY variants
    set(0x93, &CPU::op_sha, AddressingMode::IndirectIndexed, 6);
    set(0x9B, &CPU::op_shs, AddressingMode::AbsoluteY, 5);
    set(0x9C, &CPU::op_shy, AddressingMode::AbsoluteX, 5);
    set(0x9E, &CPU::op_shx, AddressingMode::AbsoluteY, 5);
    set(0x9F, &CPU::op_sha, AddressingMode::AbsoluteY, 5);

    // LAX (load into A and X)
    set(0xA3, &CPU::op_lax, AddressingMode::IndexedIndirect, 6);
    set(0xA7, &CPU::op_lax, AddressingMode::ZeroPage, 3);
    set(0xAF, &CPU::op_lax, AddressingMode::Absolute, 4);
    set(0xB3, &CPU::op_lax, AddressingMode::IndirectIndexed, 5, true);
    set(0xB7, &CPU::op_lax, AddressingMode::ZeroPageY, 4);
    set(0xBB, &CPU::op_las, AddressingMode::AbsoluteY, 4, true);
    set(0xBF, &CPU::op_lax, AddressingMode::AbsoluteY, 4, true);

    // DCP (DEC then CMP)
    set(0xC3, &CPU::op_dcp, AddressingMode::IndexedIndirect, 8);
    set(0xC7, &CPU::op_dcp, AddressingMode::ZeroPage, 5);
    set(0xCF, &CPU::op_dcp, AddressingMode::Absolute, 6);
    set(0xD3, &CPU::op_dcp, AddressingMode::IndirectIndexed, 8);
    set(0xD7, &CPU::op_dcp, AddressingMode::ZeroPageX, 6);
    set(0xDB, &CPU::op_dcp, AddressingMode::AbsoluteY, 7);
    set(0xDF, &CPU::op_dcp, AddressingMode::AbsoluteX, 7);

    // ISC (INC then SBC)
    set(0xE3, &CPU::op_isc, AddressingMode::IndexedIndirect, 8);
    set(0xE7, &CPU::op_isc, AddressingMode::ZeroPage, 5);
    set(0xEF, &CPU::op_isc, AddressingMode::Absolute, 6);
    set(0xF3, &CPU::op_isc, AddressingMode::IndirectIndexed, 8);
    set(0xF7, &CPU::op_isc, AddressingMode::ZeroPageX, 6);
    set(0xFB, &CPU::op_isc, AddressingMode::AbsoluteY, 7);
    set(0xFF, &CPU::op_isc, AddressingMode::AbsoluteX, 7);
}

void CPU::execute_instruction(uint8_t opcode) {
    const InstructionDescriptor& descriptor = dispatch_table_[opcode];
    if (descriptor.handler == nullptr) {
        illegal_instruction(opcode);
        return;
    }

    active_instruction_ = &descriptor;
    page_crossed_ = false;

    const auto handler = descriptor.handler;
    (this->*handler)(descriptor.mode);

    cycles_ += descriptor.cycles;
    if (descriptor.page_cross_cycle && page_crossed_) {
        cycles_ += 1;
    }

    active_instruction_ = nullptr;
}

void CPU::illegal_instruction(uint8_t opcode) {
    constexpr char kHexDigits[] = "0123456789ABCDEF";
    std::string message = "Encountered unimplemented opcode: 0x";
    message.push_back(kHexDigits[(opcode >> 4) & 0x0F]);
    message.push_back(kHexDigits[opcode & 0x0F]);
    throw std::runtime_error(message);
}

void CPU::op_brk(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.program_counter = static_cast<uint16_t>(registers_.program_counter + 1);
    push16(registers_.program_counter);

    const uint8_t status = static_cast<uint8_t>(registers_.status | kBreakMask | kUnusedMask);
    push(status);

    set_flag(StatusFlag::InterruptDisable, true);

    const uint8_t low = read(0xFFFE);
    const uint8_t high = read(0xFFFF);
    registers_.program_counter = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
}

void CPU::op_nop(AddressingMode mode) {
    consume_operand(mode);
}

void CPU::op_lda(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t value = read_operand(mode, page_crossed);
    registers_.accumulator = value;
    update_zero_and_negative(value);
}

void CPU::op_ldx(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t value = read_operand(mode, page_crossed);
    registers_.x = value;
    update_zero_and_negative(registers_.x);
}

void CPU::op_ldy(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t value = read_operand(mode, page_crossed);
    registers_.y = value;
    update_zero_and_negative(registers_.y);
}

void CPU::op_sta(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    page_crossed_ = page_crossed_ || page_crossed;
    write(address, registers_.accumulator);
}

void CPU::op_stx(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    page_crossed_ = page_crossed_ || page_crossed;
    write(address, registers_.x);
}

void CPU::op_sty(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    page_crossed_ = page_crossed_ || page_crossed;
    write(address, registers_.y);
}

void CPU::op_adc(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t operand = read_operand(mode, page_crossed);
    adc(operand);
}

void CPU::op_sbc(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t operand = read_operand(mode, page_crossed);
    sbc(operand);
}

void CPU::op_and(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t value = read_operand(mode, page_crossed);
    registers_.accumulator &= value;
    update_zero_and_negative(registers_.accumulator);
}

void CPU::op_ora(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t value = read_operand(mode, page_crossed);
    registers_.accumulator |= value;
    update_zero_and_negative(registers_.accumulator);
}

void CPU::op_eor(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t value = read_operand(mode, page_crossed);
    registers_.accumulator ^= value;
    update_zero_and_negative(registers_.accumulator);
}

void CPU::op_cmp(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t value = read_operand(mode, page_crossed);
    const uint8_t result = static_cast<uint8_t>(registers_.accumulator - value);
    set_flag(StatusFlag::Carry, registers_.accumulator >= value);
    update_zero_and_negative(result);
}

void CPU::op_cpx(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t value = read_operand(mode, page_crossed);
    const uint8_t result = static_cast<uint8_t>(registers_.x - value);
    set_flag(StatusFlag::Carry, registers_.x >= value);
    update_zero_and_negative(result);
}

void CPU::op_cpy(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t value = read_operand(mode, page_crossed);
    const uint8_t result = static_cast<uint8_t>(registers_.y - value);
    set_flag(StatusFlag::Carry, registers_.y >= value);
    update_zero_and_negative(result);
}

void CPU::op_bit(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t value = read_operand(mode, page_crossed);
    const uint8_t masked = static_cast<uint8_t>(registers_.accumulator & value);
    set_flag(StatusFlag::Zero, masked == 0);
    set_flag(StatusFlag::Negative, (value & 0x80) != 0);
    set_flag(StatusFlag::Overflow, (value & 0x40) != 0);
}

void CPU::op_inc(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    uint8_t value = read(address);
    value = static_cast<uint8_t>(value + 1);
    write(address, value);
    update_zero_and_negative(value);
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_dec(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    uint8_t value = read(address);
    value = static_cast<uint8_t>(value - 1);
    write(address, value);
    update_zero_and_negative(value);
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_inx(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.x = static_cast<uint8_t>(registers_.x + 1);
    update_zero_and_negative(registers_.x);
}

void CPU::op_iny(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.y = static_cast<uint8_t>(registers_.y + 1);
    update_zero_and_negative(registers_.y);
}

void CPU::op_dex(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.x = static_cast<uint8_t>(registers_.x - 1);
    update_zero_and_negative(registers_.x);
}

void CPU::op_dey(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.y = static_cast<uint8_t>(registers_.y - 1);
    update_zero_and_negative(registers_.y);
}

void CPU::op_asl(AddressingMode mode) {
    bool page_crossed = false;
    if (mode == AddressingMode::Accumulator) {
        set_flag(StatusFlag::Carry, (registers_.accumulator & 0x80) != 0);
        registers_.accumulator = static_cast<uint8_t>(registers_.accumulator << 1);
        update_zero_and_negative(registers_.accumulator);
    } else {
        const uint16_t address = compute_address(mode, page_crossed);
        uint8_t value = read(address);
        set_flag(StatusFlag::Carry, (value & 0x80) != 0);
        value = static_cast<uint8_t>(value << 1);
        write(address, value);
        update_zero_and_negative(value);
        page_crossed_ = page_crossed_ || page_crossed;
    }
}

void CPU::op_lsr(AddressingMode mode) {
    bool page_crossed = false;
    if (mode == AddressingMode::Accumulator) {
        set_flag(StatusFlag::Carry, (registers_.accumulator & 0x01) != 0);
        registers_.accumulator = static_cast<uint8_t>(registers_.accumulator >> 1);
        update_zero_and_negative(registers_.accumulator);
    } else {
        const uint16_t address = compute_address(mode, page_crossed);
        uint8_t value = read(address);
        set_flag(StatusFlag::Carry, (value & 0x01) != 0);
        value = static_cast<uint8_t>(value >> 1);
        write(address, value);
        update_zero_and_negative(value);
        page_crossed_ = page_crossed_ || page_crossed;
    }
}

void CPU::op_rol(AddressingMode mode) {
    bool page_crossed = false;
    if (mode == AddressingMode::Accumulator) {
        const bool carry_in = flag(StatusFlag::Carry);
        set_flag(StatusFlag::Carry, (registers_.accumulator & 0x80) != 0);
        registers_.accumulator = static_cast<uint8_t>((registers_.accumulator << 1) | (carry_in ? 1 : 0));
        update_zero_and_negative(registers_.accumulator);
    } else {
        const uint16_t address = compute_address(mode, page_crossed);
        uint8_t value = read(address);
        const bool carry_in = flag(StatusFlag::Carry);
        set_flag(StatusFlag::Carry, (value & 0x80) != 0);
        value = static_cast<uint8_t>((value << 1) | (carry_in ? 1 : 0));
        write(address, value);
        update_zero_and_negative(value);
        page_crossed_ = page_crossed_ || page_crossed;
    }
}

void CPU::op_ror(AddressingMode mode) {
    bool page_crossed = false;
    if (mode == AddressingMode::Accumulator) {
        const bool carry_in = flag(StatusFlag::Carry);
        set_flag(StatusFlag::Carry, (registers_.accumulator & 0x01) != 0);
        registers_.accumulator = static_cast<uint8_t>((registers_.accumulator >> 1) | (carry_in ? 0x80 : 0x00));
        update_zero_and_negative(registers_.accumulator);
    } else {
        const uint16_t address = compute_address(mode, page_crossed);
        uint8_t value = read(address);
        const bool carry_in = flag(StatusFlag::Carry);
        set_flag(StatusFlag::Carry, (value & 0x01) != 0);
        value = static_cast<uint8_t>((value >> 1) | (carry_in ? 0x80 : 0x00));
        write(address, value);
        update_zero_and_negative(value);
        page_crossed_ = page_crossed_ || page_crossed;
    }
}

void CPU::op_jsr(AddressingMode mode) {
    static_cast<void>(mode);
    const uint16_t target = fetch_word();
    const uint16_t return_address = static_cast<uint16_t>(registers_.program_counter - 1);
    push16(return_address);
    registers_.program_counter = target;
}

void CPU::op_rts(AddressingMode mode) {
    static_cast<void>(mode);
    const uint16_t address = pull16();
    registers_.program_counter = static_cast<uint16_t>(address + 1);
}

void CPU::op_rti(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.status = static_cast<uint8_t>((pull() & ~kBreakMask) | kUnusedMask);
    registers_.program_counter = pull16();
}

void CPU::op_jmp(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t target = compute_address(mode, page_crossed);
    registers_.program_counter = target;
}

void CPU::op_pha(AddressingMode mode) {
    static_cast<void>(mode);
    push(registers_.accumulator);
}

void CPU::op_php(AddressingMode mode) {
    static_cast<void>(mode);
    const uint8_t status = static_cast<uint8_t>(registers_.status | kBreakMask | kUnusedMask);
    push(status);
}

void CPU::op_pla(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.accumulator = pull();
    update_zero_and_negative(registers_.accumulator);
}

void CPU::op_plp(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.status = static_cast<uint8_t>((pull() & ~kBreakMask) | kUnusedMask);
}

void CPU::op_clc(AddressingMode mode) {
    static_cast<void>(mode);
    set_flag(StatusFlag::Carry, false);
}

void CPU::op_sec(AddressingMode mode) {
    static_cast<void>(mode);
    set_flag(StatusFlag::Carry, true);
}

void CPU::op_cli(AddressingMode mode) {
    static_cast<void>(mode);
    set_flag(StatusFlag::InterruptDisable, false);
}

void CPU::op_sei(AddressingMode mode) {
    static_cast<void>(mode);
    set_flag(StatusFlag::InterruptDisable, true);
}

void CPU::op_cld(AddressingMode mode) {
    static_cast<void>(mode);
    set_flag(StatusFlag::Decimal, false);
}

void CPU::op_sed(AddressingMode mode) {
    static_cast<void>(mode);
    set_flag(StatusFlag::Decimal, true);
}

void CPU::op_clv(AddressingMode mode) {
    static_cast<void>(mode);
    set_flag(StatusFlag::Overflow, false);
}

void CPU::op_tax(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.x = registers_.accumulator;
    update_zero_and_negative(registers_.x);
}

void CPU::op_tay(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.y = registers_.accumulator;
    update_zero_and_negative(registers_.y);
}

void CPU::op_txa(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.accumulator = registers_.x;
    update_zero_and_negative(registers_.accumulator);
}

void CPU::op_tya(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.accumulator = registers_.y;
    update_zero_and_negative(registers_.accumulator);
}

void CPU::op_tsx(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.x = registers_.stack_pointer;
    update_zero_and_negative(registers_.x);
}

void CPU::op_txs(AddressingMode mode) {
    static_cast<void>(mode);
    registers_.stack_pointer = registers_.x;
}

void CPU::op_branch(AddressingMode mode, bool condition) {
    bool page_crossed = false;
    const uint16_t target = compute_address(mode, page_crossed);
    if (condition) {
        cycles_ += 1;
        if (page_crossed) {
            cycles_ += 1;
        }
        registers_.program_counter = target;
    }
}

void CPU::op_bpl(AddressingMode mode) {
    op_branch(mode, !flag(StatusFlag::Negative));
}

void CPU::op_bmi(AddressingMode mode) {
    op_branch(mode, flag(StatusFlag::Negative));
}

void CPU::op_bvc(AddressingMode mode) {
    op_branch(mode, !flag(StatusFlag::Overflow));
}

void CPU::op_bvs(AddressingMode mode) {
    op_branch(mode, flag(StatusFlag::Overflow));
}

void CPU::op_bcc(AddressingMode mode) {
    op_branch(mode, !flag(StatusFlag::Carry));
}

void CPU::op_bcs(AddressingMode mode) {
    op_branch(mode, flag(StatusFlag::Carry));
}

void CPU::op_bne(AddressingMode mode) {
    op_branch(mode, !flag(StatusFlag::Zero));
}

void CPU::op_beq(AddressingMode mode) {
    op_branch(mode, flag(StatusFlag::Zero));
}

void CPU::op_lax(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t value = read_operand(mode, page_crossed);
    registers_.accumulator = value;
    registers_.x = value;
    update_zero_and_negative(value);
}

void CPU::op_sax(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    write(address, static_cast<uint8_t>(registers_.accumulator & registers_.x));
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_dcp(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    uint8_t value = read(address);
    value = static_cast<uint8_t>(value - 1);
    write(address, value);

    set_flag(StatusFlag::Carry, registers_.accumulator >= value);
    const uint8_t result = static_cast<uint8_t>(registers_.accumulator - value);
    update_zero_and_negative(result);
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_isc(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    uint8_t value = static_cast<uint8_t>(read(address) + 1);
    write(address, value);
    sbc(value);
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_slo(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    uint8_t value = read(address);
    set_flag(StatusFlag::Carry, (value & 0x80) != 0);
    value = static_cast<uint8_t>(value << 1);
    write(address, value);
    registers_.accumulator |= value;
    update_zero_and_negative(registers_.accumulator);
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_rla(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    uint8_t value = read(address);
    const bool carry_in = flag(StatusFlag::Carry);
    set_flag(StatusFlag::Carry, (value & 0x80) != 0);
    value = static_cast<uint8_t>((value << 1) | (carry_in ? 1 : 0));
    write(address, value);
    registers_.accumulator &= value;
    update_zero_and_negative(registers_.accumulator);
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_sre(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    uint8_t value = read(address);
    set_flag(StatusFlag::Carry, (value & 0x01) != 0);
    value = static_cast<uint8_t>(value >> 1);
    write(address, value);
    registers_.accumulator ^= value;
    update_zero_and_negative(registers_.accumulator);
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_rra(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    uint8_t value = read(address);
    const bool carry_in = flag(StatusFlag::Carry);
    set_flag(StatusFlag::Carry, (value & 0x01) != 0);
    value = static_cast<uint8_t>((value >> 1) | (carry_in ? 0x80 : 0x00));
    write(address, value);
    adc(value);
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_anc(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t operand = read_operand(mode, page_crossed);
    registers_.accumulator = static_cast<uint8_t>(registers_.accumulator & operand);
    update_zero_and_negative(registers_.accumulator);
    set_flag(StatusFlag::Carry, (registers_.accumulator & 0x80) != 0);
}

void CPU::op_alr(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t operand = read_operand(mode, page_crossed);
    uint8_t value = static_cast<uint8_t>(registers_.accumulator & operand);
    set_flag(StatusFlag::Carry, (value & 0x01) != 0);
    value = static_cast<uint8_t>(value >> 1);
    registers_.accumulator = value;
    update_zero_and_negative(registers_.accumulator);
}

void CPU::op_arr(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t operand = read_operand(mode, page_crossed);
    const uint8_t masked = static_cast<uint8_t>(registers_.accumulator & operand);
    const bool carry_in = flag(StatusFlag::Carry);
    const uint8_t result = static_cast<uint8_t>((masked >> 1) | (carry_in ? 0x80 : 0x00));

    registers_.accumulator = result;
    const bool bit6 = (result & 0x40) != 0;
    const bool bit5 = (result & 0x20) != 0;
    set_flag(StatusFlag::Carry, bit6);
    set_flag(StatusFlag::Overflow, bit6 ^ bit5);
    update_zero_and_negative(result);
}

void CPU::op_ane(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t operand = read_operand(mode, page_crossed);
    const uint8_t value = static_cast<uint8_t>((registers_.accumulator | 0xEE) & registers_.x & operand);
    registers_.accumulator = value;
    update_zero_and_negative(value);
}

void CPU::op_lxa(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t operand = read_operand(mode, page_crossed);
    const uint8_t value = static_cast<uint8_t>((registers_.accumulator | 0xEE) & operand);
    registers_.accumulator = value;
    registers_.x = value;
    update_zero_and_negative(value);
}

void CPU::op_las(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    uint8_t value = read(address);
    value &= registers_.stack_pointer;
    registers_.accumulator = value;
    registers_.x = value;
    registers_.stack_pointer = value;
    update_zero_and_negative(value);
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_sbx(AddressingMode mode) {
    bool page_crossed = false;
    const uint8_t operand = read_operand(mode, page_crossed);
    const uint16_t combined = static_cast<uint16_t>(registers_.accumulator & registers_.x);
    const uint16_t result = (combined - operand) & 0x1FF;
    set_flag(StatusFlag::Carry, combined >= operand);
    registers_.x = static_cast<uint8_t>(result & 0xFF);
    update_zero_and_negative(registers_.x);
}

void CPU::op_sha(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    uint8_t value = static_cast<uint8_t>(registers_.accumulator & registers_.x);
    if (mode == AddressingMode::IndirectIndexed || mode == AddressingMode::AbsoluteY) {
        const uint8_t adjust = static_cast<uint8_t>(((address >> 8) & 0xFF) + 1);
        value = static_cast<uint8_t>(value & adjust);
    }
    write(address, value);
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_shs(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    const uint8_t value = static_cast<uint8_t>(registers_.accumulator & registers_.x);
    registers_.stack_pointer = value;
    const uint8_t adjust = static_cast<uint8_t>(((address >> 8) & 0xFF) + 1);
    write(address, static_cast<uint8_t>(value & adjust));
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_shx(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    const uint8_t adjust = static_cast<uint8_t>(((address >> 8) & 0xFF) + 1);
    write(address, static_cast<uint8_t>(registers_.x & adjust));
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_shy(AddressingMode mode) {
    bool page_crossed = false;
    const uint16_t address = compute_address(mode, page_crossed);
    const uint8_t adjust = static_cast<uint8_t>(((address >> 8) & 0xFF) + 1);
    write(address, static_cast<uint8_t>(registers_.y & adjust));
    page_crossed_ = page_crossed_ || page_crossed;
}

void CPU::op_jam(AddressingMode mode) {
    static_cast<void>(mode);
    throw std::runtime_error("CPU entered illegal JAM state");
}

}  // namespace emulator
