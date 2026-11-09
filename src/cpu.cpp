#include "cpu.hpp"

#include <initializer_list>
#include <stdexcept>

#include "bus.hpp"

namespace emulator {

namespace {

constexpr uint16_t kStackPage = 0x0100;
constexpr uint16_t kResetVector = 0xFFFC;
constexpr uint16_t kInterruptVector = 0xFFFE;

constexpr uint8_t bit(CPU::StatusFlag flag) {
    return static_cast<uint8_t>(flag);
}

// SHA, SHX, SHY and SHS store a register ANDed with this odd value: the high
// byte of the target address, plus one.
constexpr uint8_t high_byte_plus_one(uint16_t address) {
    return static_cast<uint8_t>((address >> 8) + 1);
}

}  // namespace

CPU::CPU(Bus& bus) : bus_(bus) {
    build_instruction_table();
}

void CPU::reset() {
    registers_ = Registers{};
    registers_.program_counter = read_word(kResetVector);
    cycles_ = 0;
}

void CPU::step() {
    const Instruction& instruction = instructions_[fetch_byte()];

    page_crossed_ = false;
    (this->*instruction.handler)(instruction.mode);

    cycles_ += instruction.cycles;
    if (instruction.page_cross_penalty && page_crossed_) {
        cycles_ += 1;
    }
}

// ---------------------------------------------------------------------------
// Memory access
// ---------------------------------------------------------------------------

uint8_t CPU::read(uint16_t address) const {
    return bus_.read(address);
}

void CPU::write(uint16_t address, uint8_t value) {
    bus_.write(address, value);
}

uint8_t CPU::fetch_byte() {
    return read(registers_.program_counter++);
}

uint16_t CPU::fetch_word() {
    const uint8_t low = fetch_byte();
    const uint8_t high = fetch_byte();
    return static_cast<uint16_t>(high << 8 | low);
}

// Reads a little-endian 16-bit value. When the 6502 steps from the low byte to
// the high byte it never carries into the upper half of the pointer, so a
// pointer at $12FF takes its high byte from $1200, not $1300. This one rule
// produces both the well-known JMP ($xxFF) bug and the wrap-around of
// zero-page pointers.
uint16_t CPU::read_word(uint16_t pointer) const {
    const uint16_t next = static_cast<uint16_t>((pointer & 0xFF00) | ((pointer + 1) & 0x00FF));
    return static_cast<uint16_t>(read(next) << 8 | read(pointer));
}

// ---------------------------------------------------------------------------
// Stack: lives in page $01 and grows downwards.
// ---------------------------------------------------------------------------

void CPU::push(uint8_t value) {
    write(kStackPage + registers_.stack_pointer, value);
    registers_.stack_pointer--;
}

void CPU::push_word(uint16_t value) {
    push(static_cast<uint8_t>(value >> 8));
    push(static_cast<uint8_t>(value & 0xFF));
}

uint8_t CPU::pull() {
    registers_.stack_pointer++;
    return read(kStackPage + registers_.stack_pointer);
}

uint16_t CPU::pull_word() {
    const uint8_t low = pull();
    const uint8_t high = pull();
    return static_cast<uint16_t>(high << 8 | low);
}

// The Break flag is not a real bit in the register. It only exists in the copy
// of the status byte that PHP and BRK push, and is dropped again on the way
// back in.
void CPU::push_status() {
    push(registers_.status | bit(Break) | bit(Unused));
}

void CPU::pull_status() {
    registers_.status = static_cast<uint8_t>((pull() & ~bit(Break)) | bit(Unused));
}

// ---------------------------------------------------------------------------
// Operands
// ---------------------------------------------------------------------------

// Adds an index or branch offset to an address and records whether the result
// landed in a different 256-byte page, which costs some instructions a cycle.
uint16_t CPU::offset_address(uint16_t base, int offset) {
    const uint16_t address = static_cast<uint16_t>(base + offset);
    if ((address & 0xFF00) != (base & 0xFF00)) {
        page_crossed_ = true;
    }
    return address;
}

// Consumes the operand bytes that follow the opcode and returns the address
// the instruction should work on.
uint16_t CPU::operand_address(AddressingMode mode) {
    switch (mode) {
        case Immediate:
            return registers_.program_counter++;
        case ZeroPage:
            return fetch_byte();
        case ZeroPageX:
            return static_cast<uint8_t>(fetch_byte() + registers_.x);  // Wraps within page zero.
        case ZeroPageY:
            return static_cast<uint8_t>(fetch_byte() + registers_.y);
        case Absolute:
            return fetch_word();
        case AbsoluteX:
            return offset_address(fetch_word(), registers_.x);
        case AbsoluteY:
            return offset_address(fetch_word(), registers_.y);
        case Indirect:
            return read_word(fetch_word());
        case IndexedIndirect:
            return read_word(static_cast<uint8_t>(fetch_byte() + registers_.x));
        case IndirectIndexed:
            return offset_address(read_word(fetch_byte()), registers_.y);
        case Relative: {
            const int8_t offset = static_cast<int8_t>(fetch_byte());
            return offset_address(registers_.program_counter, offset);
        }
        case Implied:
        case Accumulator:
            break;
    }
    return 0;  // Implied and Accumulator instructions have no address.
}

uint8_t CPU::read_operand(AddressingMode mode) {
    return mode == Accumulator ? registers_.accumulator : read(operand_address(mode));
}

// Read-modify-write: runs `operation` on the operand, stores the result back
// where it came from (the accumulator or memory), and returns it.
uint8_t CPU::modify_operand(AddressingMode mode, uint8_t (CPU::*operation)(uint8_t)) {
    if (mode == Accumulator) {
        registers_.accumulator = (this->*operation)(registers_.accumulator);
        return registers_.accumulator;
    }

    const uint16_t address = operand_address(mode);
    const uint8_t result = (this->*operation)(read(address));
    write(address, result);
    return result;
}

// ---------------------------------------------------------------------------
// Flags
// ---------------------------------------------------------------------------

bool CPU::flag(StatusFlag flag) const {
    return (registers_.status & bit(flag)) != 0;
}

void CPU::set_flag(StatusFlag flag, bool value) {
    if (value) {
        registers_.status |= bit(flag);
    } else {
        registers_.status &= ~bit(flag);
    }
}

void CPU::update_zero_and_negative(uint8_t value) {
    set_flag(Zero, value == 0);
    set_flag(Negative, (value & 0x80) != 0);
}

// Almost every instruction that changes A, X or Y also updates Zero and
// Negative from the new value.
void CPU::set_register(uint8_t& target, uint8_t value) {
    target = value;
    update_zero_and_negative(value);
}

// ---------------------------------------------------------------------------
// Arithmetic shared by several instructions
// ---------------------------------------------------------------------------

void CPU::add_with_carry(uint8_t operand) {
    const uint8_t a = registers_.accumulator;
    const int carry_in = flag(Carry) ? 1 : 0;
    int sum = a + operand + carry_in;

    // Signed overflow: both inputs have the same sign and the result does not.
    // This is always judged on the plain binary sum, even in decimal mode.
    set_flag(Overflow, (~(a ^ operand) & (a ^ sum) & 0x80) != 0);

    if (flag(Decimal)) {
        // Each 4-bit digit holds 0-9. A digit that overflowed past 9 is pushed
        // on to the next one by adding 6, which skips the unused values A-F.
        if ((a & 0x0F) + (operand & 0x0F) + carry_in > 0x09) {
            sum += 0x06;
        }
        if (sum > 0x99) {
            sum += 0x60;
        }
        set_flag(Carry, sum > 0x99);
    } else {
        set_flag(Carry, sum > 0xFF);
    }

    set_register(registers_.accumulator, static_cast<uint8_t>(sum));
}

void CPU::subtract_with_carry(uint8_t operand) {
    const uint8_t a = registers_.accumulator;
    // For subtraction the carry flag works backwards: clear means "borrow".
    const int borrow_in = flag(Carry) ? 0 : 1;
    const int difference = a - operand - borrow_in;

    set_flag(Overflow, ((a ^ difference) & (a ^ operand) & 0x80) != 0);

    if (flag(Decimal)) {
        // Subtract one decimal digit at a time, borrowing 10 when a digit
        // goes negative.
        int low = (a & 0x0F) - (operand & 0x0F) - borrow_in;
        const int low_borrow = low < 0 ? 1 : 0;
        if (low < 0) {
            low += 10;
        }

        int high = (a >> 4) - (operand >> 4) - low_borrow;
        set_flag(Carry, high >= 0);
        if (high < 0) {
            high += 10;
        }

        set_register(registers_.accumulator, static_cast<uint8_t>(high << 4 | (low & 0x0F)));
    } else {
        set_flag(Carry, difference >= 0);
        set_register(registers_.accumulator, static_cast<uint8_t>(difference));
    }
}

// CMP, CPX and CPY subtract without storing the result; only the flags change.
void CPU::compare(uint8_t register_value, uint8_t operand) {
    set_flag(Carry, register_value >= operand);
    update_zero_and_negative(static_cast<uint8_t>(register_value - operand));
}

// A branch costs one extra cycle when taken, and another if the target is in
// a different page.
void CPU::branch_if(bool condition) {
    const uint16_t target = operand_address(Relative);
    if (condition) {
        cycles_ += page_crossed_ ? 2 : 1;
        registers_.program_counter = target;
    }
}

// The shifts and rotates move the bit that falls off the end into Carry.
// Rotates also bring the old Carry in at the other end.
uint8_t CPU::shift_left(uint8_t value) {
    set_flag(Carry, (value & 0x80) != 0);
    value = static_cast<uint8_t>(value << 1);
    update_zero_and_negative(value);
    return value;
}

uint8_t CPU::shift_right(uint8_t value) {
    set_flag(Carry, (value & 0x01) != 0);
    value = static_cast<uint8_t>(value >> 1);
    update_zero_and_negative(value);
    return value;
}

uint8_t CPU::rotate_left(uint8_t value) {
    const uint8_t carry_in = flag(Carry) ? 0x01 : 0x00;
    set_flag(Carry, (value & 0x80) != 0);
    value = static_cast<uint8_t>(value << 1 | carry_in);
    update_zero_and_negative(value);
    return value;
}

uint8_t CPU::rotate_right(uint8_t value) {
    const uint8_t carry_in = flag(Carry) ? 0x80 : 0x00;
    set_flag(Carry, (value & 0x01) != 0);
    value = static_cast<uint8_t>(value >> 1 | carry_in);
    update_zero_and_negative(value);
    return value;
}

uint8_t CPU::increment(uint8_t value) {
    value++;
    update_zero_and_negative(value);
    return value;
}

uint8_t CPU::decrement(uint8_t value) {
    value--;
    update_zero_and_negative(value);
    return value;
}

// ---------------------------------------------------------------------------
// Opcode table
// ---------------------------------------------------------------------------

void CPU::build_instruction_table() {
    // Passed as the last argument for instructions that take one extra cycle
    // when indexing crosses a page boundary.
    constexpr bool kPageCrossPenalty = true;

    const auto set = [this](uint8_t opcode, const char* mnemonic, Handler handler, AddressingMode mode,
                            uint8_t cycles, bool page_cross_penalty = false) {
        instructions_[opcode] = Instruction{mnemonic, handler, mode, cycles, page_cross_penalty};
    };

    // --- Load/Store ---
    set(0xA9, "LDA", &CPU::op_lda, Immediate, 2);
    set(0xA5, "LDA", &CPU::op_lda, ZeroPage, 3);
    set(0xB5, "LDA", &CPU::op_lda, ZeroPageX, 4);
    set(0xAD, "LDA", &CPU::op_lda, Absolute, 4);
    set(0xBD, "LDA", &CPU::op_lda, AbsoluteX, 4, kPageCrossPenalty);
    set(0xB9, "LDA", &CPU::op_lda, AbsoluteY, 4, kPageCrossPenalty);
    set(0xA1, "LDA", &CPU::op_lda, IndexedIndirect, 6);
    set(0xB1, "LDA", &CPU::op_lda, IndirectIndexed, 5, kPageCrossPenalty);

    set(0xA2, "LDX", &CPU::op_ldx, Immediate, 2);
    set(0xA6, "LDX", &CPU::op_ldx, ZeroPage, 3);
    set(0xB6, "LDX", &CPU::op_ldx, ZeroPageY, 4);
    set(0xAE, "LDX", &CPU::op_ldx, Absolute, 4);
    set(0xBE, "LDX", &CPU::op_ldx, AbsoluteY, 4, kPageCrossPenalty);

    set(0xA0, "LDY", &CPU::op_ldy, Immediate, 2);
    set(0xA4, "LDY", &CPU::op_ldy, ZeroPage, 3);
    set(0xB4, "LDY", &CPU::op_ldy, ZeroPageX, 4);
    set(0xAC, "LDY", &CPU::op_ldy, Absolute, 4);
    set(0xBC, "LDY", &CPU::op_ldy, AbsoluteX, 4, kPageCrossPenalty);

    set(0x85, "STA", &CPU::op_sta, ZeroPage, 3);
    set(0x95, "STA", &CPU::op_sta, ZeroPageX, 4);
    set(0x8D, "STA", &CPU::op_sta, Absolute, 4);
    set(0x9D, "STA", &CPU::op_sta, AbsoluteX, 5);
    set(0x99, "STA", &CPU::op_sta, AbsoluteY, 5);
    set(0x81, "STA", &CPU::op_sta, IndexedIndirect, 6);
    set(0x91, "STA", &CPU::op_sta, IndirectIndexed, 6);

    set(0x86, "STX", &CPU::op_stx, ZeroPage, 3);
    set(0x96, "STX", &CPU::op_stx, ZeroPageY, 4);
    set(0x8E, "STX", &CPU::op_stx, Absolute, 4);

    set(0x84, "STY", &CPU::op_sty, ZeroPage, 3);
    set(0x94, "STY", &CPU::op_sty, ZeroPageX, 4);
    set(0x8C, "STY", &CPU::op_sty, Absolute, 4);

    // --- Arithmetic / Logic ---
    set(0x69, "ADC", &CPU::op_adc, Immediate, 2);
    set(0x65, "ADC", &CPU::op_adc, ZeroPage, 3);
    set(0x75, "ADC", &CPU::op_adc, ZeroPageX, 4);
    set(0x6D, "ADC", &CPU::op_adc, Absolute, 4);
    set(0x7D, "ADC", &CPU::op_adc, AbsoluteX, 4, kPageCrossPenalty);
    set(0x79, "ADC", &CPU::op_adc, AbsoluteY, 4, kPageCrossPenalty);
    set(0x61, "ADC", &CPU::op_adc, IndexedIndirect, 6);
    set(0x71, "ADC", &CPU::op_adc, IndirectIndexed, 5, kPageCrossPenalty);

    set(0xE9, "SBC", &CPU::op_sbc, Immediate, 2);
    set(0xE5, "SBC", &CPU::op_sbc, ZeroPage, 3);
    set(0xF5, "SBC", &CPU::op_sbc, ZeroPageX, 4);
    set(0xED, "SBC", &CPU::op_sbc, Absolute, 4);
    set(0xFD, "SBC", &CPU::op_sbc, AbsoluteX, 4, kPageCrossPenalty);
    set(0xF9, "SBC", &CPU::op_sbc, AbsoluteY, 4, kPageCrossPenalty);
    set(0xE1, "SBC", &CPU::op_sbc, IndexedIndirect, 6);
    set(0xF1, "SBC", &CPU::op_sbc, IndirectIndexed, 5, kPageCrossPenalty);

    set(0x29, "AND", &CPU::op_and, Immediate, 2);
    set(0x25, "AND", &CPU::op_and, ZeroPage, 3);
    set(0x35, "AND", &CPU::op_and, ZeroPageX, 4);
    set(0x2D, "AND", &CPU::op_and, Absolute, 4);
    set(0x3D, "AND", &CPU::op_and, AbsoluteX, 4, kPageCrossPenalty);
    set(0x39, "AND", &CPU::op_and, AbsoluteY, 4, kPageCrossPenalty);
    set(0x21, "AND", &CPU::op_and, IndexedIndirect, 6);
    set(0x31, "AND", &CPU::op_and, IndirectIndexed, 5, kPageCrossPenalty);

    set(0x09, "ORA", &CPU::op_ora, Immediate, 2);
    set(0x05, "ORA", &CPU::op_ora, ZeroPage, 3);
    set(0x15, "ORA", &CPU::op_ora, ZeroPageX, 4);
    set(0x0D, "ORA", &CPU::op_ora, Absolute, 4);
    set(0x1D, "ORA", &CPU::op_ora, AbsoluteX, 4, kPageCrossPenalty);
    set(0x19, "ORA", &CPU::op_ora, AbsoluteY, 4, kPageCrossPenalty);
    set(0x01, "ORA", &CPU::op_ora, IndexedIndirect, 6);
    set(0x11, "ORA", &CPU::op_ora, IndirectIndexed, 5, kPageCrossPenalty);

    set(0x49, "EOR", &CPU::op_eor, Immediate, 2);
    set(0x45, "EOR", &CPU::op_eor, ZeroPage, 3);
    set(0x55, "EOR", &CPU::op_eor, ZeroPageX, 4);
    set(0x4D, "EOR", &CPU::op_eor, Absolute, 4);
    set(0x5D, "EOR", &CPU::op_eor, AbsoluteX, 4, kPageCrossPenalty);
    set(0x59, "EOR", &CPU::op_eor, AbsoluteY, 4, kPageCrossPenalty);
    set(0x41, "EOR", &CPU::op_eor, IndexedIndirect, 6);
    set(0x51, "EOR", &CPU::op_eor, IndirectIndexed, 5, kPageCrossPenalty);

    set(0xC9, "CMP", &CPU::op_cmp, Immediate, 2);
    set(0xC5, "CMP", &CPU::op_cmp, ZeroPage, 3);
    set(0xD5, "CMP", &CPU::op_cmp, ZeroPageX, 4);
    set(0xCD, "CMP", &CPU::op_cmp, Absolute, 4);
    set(0xDD, "CMP", &CPU::op_cmp, AbsoluteX, 4, kPageCrossPenalty);
    set(0xD9, "CMP", &CPU::op_cmp, AbsoluteY, 4, kPageCrossPenalty);
    set(0xC1, "CMP", &CPU::op_cmp, IndexedIndirect, 6);
    set(0xD1, "CMP", &CPU::op_cmp, IndirectIndexed, 5, kPageCrossPenalty);

    set(0xE0, "CPX", &CPU::op_cpx, Immediate, 2);
    set(0xE4, "CPX", &CPU::op_cpx, ZeroPage, 3);
    set(0xEC, "CPX", &CPU::op_cpx, Absolute, 4);

    set(0xC0, "CPY", &CPU::op_cpy, Immediate, 2);
    set(0xC4, "CPY", &CPU::op_cpy, ZeroPage, 3);
    set(0xCC, "CPY", &CPU::op_cpy, Absolute, 4);

    set(0x24, "BIT", &CPU::op_bit, ZeroPage, 3);
    set(0x2C, "BIT", &CPU::op_bit, Absolute, 4);

    // --- Increment / Decrement ---
    set(0xE6, "INC", &CPU::op_inc, ZeroPage, 5);
    set(0xF6, "INC", &CPU::op_inc, ZeroPageX, 6);
    set(0xEE, "INC", &CPU::op_inc, Absolute, 6);
    set(0xFE, "INC", &CPU::op_inc, AbsoluteX, 7);

    set(0xC6, "DEC", &CPU::op_dec, ZeroPage, 5);
    set(0xD6, "DEC", &CPU::op_dec, ZeroPageX, 6);
    set(0xCE, "DEC", &CPU::op_dec, Absolute, 6);
    set(0xDE, "DEC", &CPU::op_dec, AbsoluteX, 7);

    set(0xE8, "INX", &CPU::op_inx, Implied, 2);
    set(0xC8, "INY", &CPU::op_iny, Implied, 2);
    set(0xCA, "DEX", &CPU::op_dex, Implied, 2);
    set(0x88, "DEY", &CPU::op_dey, Implied, 2);

    // --- Shift / Rotate ---
    set(0x0A, "ASL", &CPU::op_asl, Accumulator, 2);
    set(0x06, "ASL", &CPU::op_asl, ZeroPage, 5);
    set(0x16, "ASL", &CPU::op_asl, ZeroPageX, 6);
    set(0x0E, "ASL", &CPU::op_asl, Absolute, 6);
    set(0x1E, "ASL", &CPU::op_asl, AbsoluteX, 7);

    set(0x4A, "LSR", &CPU::op_lsr, Accumulator, 2);
    set(0x46, "LSR", &CPU::op_lsr, ZeroPage, 5);
    set(0x56, "LSR", &CPU::op_lsr, ZeroPageX, 6);
    set(0x4E, "LSR", &CPU::op_lsr, Absolute, 6);
    set(0x5E, "LSR", &CPU::op_lsr, AbsoluteX, 7);

    set(0x2A, "ROL", &CPU::op_rol, Accumulator, 2);
    set(0x26, "ROL", &CPU::op_rol, ZeroPage, 5);
    set(0x36, "ROL", &CPU::op_rol, ZeroPageX, 6);
    set(0x2E, "ROL", &CPU::op_rol, Absolute, 6);
    set(0x3E, "ROL", &CPU::op_rol, AbsoluteX, 7);

    set(0x6A, "ROR", &CPU::op_ror, Accumulator, 2);
    set(0x66, "ROR", &CPU::op_ror, ZeroPage, 5);
    set(0x76, "ROR", &CPU::op_ror, ZeroPageX, 6);
    set(0x6E, "ROR", &CPU::op_ror, Absolute, 6);
    set(0x7E, "ROR", &CPU::op_ror, AbsoluteX, 7);

    // --- Branches ---
    set(0x10, "BPL", &CPU::op_bpl, Relative, 2);
    set(0x30, "BMI", &CPU::op_bmi, Relative, 2);
    set(0x50, "BVC", &CPU::op_bvc, Relative, 2);
    set(0x70, "BVS", &CPU::op_bvs, Relative, 2);
    set(0x90, "BCC", &CPU::op_bcc, Relative, 2);
    set(0xB0, "BCS", &CPU::op_bcs, Relative, 2);
    set(0xD0, "BNE", &CPU::op_bne, Relative, 2);
    set(0xF0, "BEQ", &CPU::op_beq, Relative, 2);

    // --- Transfer / Stack ---
    set(0xAA, "TAX", &CPU::op_tax, Implied, 2);
    set(0xA8, "TAY", &CPU::op_tay, Implied, 2);
    set(0x8A, "TXA", &CPU::op_txa, Implied, 2);
    set(0x98, "TYA", &CPU::op_tya, Implied, 2);
    set(0xBA, "TSX", &CPU::op_tsx, Implied, 2);
    set(0x9A, "TXS", &CPU::op_txs, Implied, 2);

    set(0x48, "PHA", &CPU::op_pha, Implied, 3);
    set(0x08, "PHP", &CPU::op_php, Implied, 3);
    set(0x68, "PLA", &CPU::op_pla, Implied, 4);
    set(0x28, "PLP", &CPU::op_plp, Implied, 4);

    // --- Status ---
    set(0x18, "CLC", &CPU::op_clc, Implied, 2);
    set(0x38, "SEC", &CPU::op_sec, Implied, 2);
    set(0x58, "CLI", &CPU::op_cli, Implied, 2);
    set(0x78, "SEI", &CPU::op_sei, Implied, 2);
    set(0xB8, "CLV", &CPU::op_clv, Implied, 2);
    set(0xD8, "CLD", &CPU::op_cld, Implied, 2);
    set(0xF8, "SED", &CPU::op_sed, Implied, 2);

    // --- Control ---
    set(0x00, "BRK", &CPU::op_brk, Implied, 7);
    set(0xEA, "NOP", &CPU::op_nop, Implied, 2);
    set(0x4C, "JMP", &CPU::op_jmp, Absolute, 3);
    set(0x6C, "JMP", &CPU::op_jmp, Indirect, 5);
    set(0x20, "JSR", &CPU::op_jsr, Absolute, 6);
    set(0x40, "RTI", &CPU::op_rti, Implied, 6);
    set(0x60, "RTS", &CPU::op_rts, Implied, 6);

    // --- Undocumented operations ---
    for (const uint8_t opcode : {0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72, 0x92, 0xB2, 0xD2, 0xF2}) {
        set(opcode, "JAM", &CPU::op_jam, Implied, 0);
    }

    for (const uint8_t opcode : {0x1A, 0x3A, 0x5A, 0x7A, 0xDA, 0xFA}) {
        set(opcode, "NOP", &CPU::op_nop, Implied, 2);
    }

    for (const uint8_t opcode : {0x80, 0x82, 0x89, 0xC2, 0xE2}) {
        set(opcode, "NOP", &CPU::op_nop, Immediate, 2);
    }

    for (const uint8_t opcode : {0x04, 0x44, 0x64}) {
        set(opcode, "NOP", &CPU::op_nop, ZeroPage, 3);
    }

    for (const uint8_t opcode : {0x14, 0x34, 0x54, 0x74, 0xD4, 0xF4}) {
        set(opcode, "NOP", &CPU::op_nop, ZeroPageX, 4);
    }

    set(0x0C, "NOP", &CPU::op_nop, Absolute, 4);
    for (const uint8_t opcode : {0x1C, 0x3C, 0x5C, 0x7C, 0xDC, 0xFC}) {
        set(opcode, "NOP", &CPU::op_nop, AbsoluteX, 4, kPageCrossPenalty);
    }

    for (const uint8_t opcode : {0x0B, 0x2B}) {
        set(opcode, "ANC", &CPU::op_anc, Immediate, 2);
    }
    set(0x4B, "ALR", &CPU::op_alr, Immediate, 2);
    set(0x6B, "ARR", &CPU::op_arr, Immediate, 2);
    set(0x8B, "ANE", &CPU::op_ane, Immediate, 2);
    set(0xAB, "LXA", &CPU::op_lxa, Immediate, 2);
    set(0xCB, "SBX", &CPU::op_sbx, Immediate, 2);
    set(0xEB, "SBC", &CPU::op_sbc, Immediate, 2);

    // SLO (ASL then ORA)
    set(0x03, "SLO", &CPU::op_slo, IndexedIndirect, 8);
    set(0x07, "SLO", &CPU::op_slo, ZeroPage, 5);
    set(0x0F, "SLO", &CPU::op_slo, Absolute, 6);
    set(0x13, "SLO", &CPU::op_slo, IndirectIndexed, 8);
    set(0x17, "SLO", &CPU::op_slo, ZeroPageX, 6);
    set(0x1B, "SLO", &CPU::op_slo, AbsoluteY, 7);
    set(0x1F, "SLO", &CPU::op_slo, AbsoluteX, 7);

    // RLA (ROL then AND)
    set(0x23, "RLA", &CPU::op_rla, IndexedIndirect, 8);
    set(0x27, "RLA", &CPU::op_rla, ZeroPage, 5);
    set(0x2F, "RLA", &CPU::op_rla, Absolute, 6);
    set(0x33, "RLA", &CPU::op_rla, IndirectIndexed, 8);
    set(0x37, "RLA", &CPU::op_rla, ZeroPageX, 6);
    set(0x3B, "RLA", &CPU::op_rla, AbsoluteY, 7);
    set(0x3F, "RLA", &CPU::op_rla, AbsoluteX, 7);

    // SRE (LSR then EOR)
    set(0x43, "SRE", &CPU::op_sre, IndexedIndirect, 8);
    set(0x47, "SRE", &CPU::op_sre, ZeroPage, 5);
    set(0x4F, "SRE", &CPU::op_sre, Absolute, 6);
    set(0x53, "SRE", &CPU::op_sre, IndirectIndexed, 8);
    set(0x57, "SRE", &CPU::op_sre, ZeroPageX, 6);
    set(0x5B, "SRE", &CPU::op_sre, AbsoluteY, 7);
    set(0x5F, "SRE", &CPU::op_sre, AbsoluteX, 7);

    // RRA (ROR then ADC)
    set(0x63, "RRA", &CPU::op_rra, IndexedIndirect, 8);
    set(0x67, "RRA", &CPU::op_rra, ZeroPage, 5);
    set(0x6F, "RRA", &CPU::op_rra, Absolute, 6);
    set(0x73, "RRA", &CPU::op_rra, IndirectIndexed, 8);
    set(0x77, "RRA", &CPU::op_rra, ZeroPageX, 6);
    set(0x7B, "RRA", &CPU::op_rra, AbsoluteY, 7);
    set(0x7F, "RRA", &CPU::op_rra, AbsoluteX, 7);

    // SAX (store A & X)
    set(0x83, "SAX", &CPU::op_sax, IndexedIndirect, 6);
    set(0x87, "SAX", &CPU::op_sax, ZeroPage, 3);
    set(0x8F, "SAX", &CPU::op_sax, Absolute, 4);
    set(0x97, "SAX", &CPU::op_sax, ZeroPageY, 4);

    // SHA / SHS / SHX / SHY variants
    set(0x93, "SHA", &CPU::op_sha, IndirectIndexed, 6);
    set(0x9B, "SHS", &CPU::op_shs, AbsoluteY, 5);
    set(0x9C, "SHY", &CPU::op_shy, AbsoluteX, 5);
    set(0x9E, "SHX", &CPU::op_shx, AbsoluteY, 5);
    set(0x9F, "SHA", &CPU::op_sha, AbsoluteY, 5);

    // LAX (load into A and X)
    set(0xA3, "LAX", &CPU::op_lax, IndexedIndirect, 6);
    set(0xA7, "LAX", &CPU::op_lax, ZeroPage, 3);
    set(0xAF, "LAX", &CPU::op_lax, Absolute, 4);
    set(0xB3, "LAX", &CPU::op_lax, IndirectIndexed, 5, kPageCrossPenalty);
    set(0xB7, "LAX", &CPU::op_lax, ZeroPageY, 4);
    set(0xBB, "LAS", &CPU::op_las, AbsoluteY, 4, kPageCrossPenalty);
    set(0xBF, "LAX", &CPU::op_lax, AbsoluteY, 4, kPageCrossPenalty);

    // DCP (DEC then CMP)
    set(0xC3, "DCP", &CPU::op_dcp, IndexedIndirect, 8);
    set(0xC7, "DCP", &CPU::op_dcp, ZeroPage, 5);
    set(0xCF, "DCP", &CPU::op_dcp, Absolute, 6);
    set(0xD3, "DCP", &CPU::op_dcp, IndirectIndexed, 8);
    set(0xD7, "DCP", &CPU::op_dcp, ZeroPageX, 6);
    set(0xDB, "DCP", &CPU::op_dcp, AbsoluteY, 7);
    set(0xDF, "DCP", &CPU::op_dcp, AbsoluteX, 7);

    // ISC (INC then SBC)
    set(0xE3, "ISC", &CPU::op_isc, IndexedIndirect, 8);
    set(0xE7, "ISC", &CPU::op_isc, ZeroPage, 5);
    set(0xEF, "ISC", &CPU::op_isc, Absolute, 6);
    set(0xF3, "ISC", &CPU::op_isc, IndirectIndexed, 8);
    set(0xF7, "ISC", &CPU::op_isc, ZeroPageX, 6);
    set(0xFB, "ISC", &CPU::op_isc, AbsoluteY, 7);
    set(0xFF, "ISC", &CPU::op_isc, AbsoluteX, 7);
}

// ---------------------------------------------------------------------------
// Documented instructions
//
// Every handler takes the addressing mode from its table entry. Handlers for
// instructions with no operand leave the parameter unnamed.
// ---------------------------------------------------------------------------

// --- Load and store ---

void CPU::op_lda(AddressingMode mode) {
    set_register(registers_.accumulator, read_operand(mode));
}

void CPU::op_ldx(AddressingMode mode) {
    set_register(registers_.x, read_operand(mode));
}

void CPU::op_ldy(AddressingMode mode) {
    set_register(registers_.y, read_operand(mode));
}

void CPU::op_sta(AddressingMode mode) {
    write(operand_address(mode), registers_.accumulator);
}

void CPU::op_stx(AddressingMode mode) {
    write(operand_address(mode), registers_.x);
}

void CPU::op_sty(AddressingMode mode) {
    write(operand_address(mode), registers_.y);
}

// --- Register transfers ---

void CPU::op_tax(AddressingMode) {
    set_register(registers_.x, registers_.accumulator);
}

void CPU::op_tay(AddressingMode) {
    set_register(registers_.y, registers_.accumulator);
}

void CPU::op_txa(AddressingMode) {
    set_register(registers_.accumulator, registers_.x);
}

void CPU::op_tya(AddressingMode) {
    set_register(registers_.accumulator, registers_.y);
}

void CPU::op_tsx(AddressingMode) {
    set_register(registers_.x, registers_.stack_pointer);
}

// TXS is the one transfer that leaves the flags alone.
void CPU::op_txs(AddressingMode) {
    registers_.stack_pointer = registers_.x;
}

// --- Stack ---

void CPU::op_pha(AddressingMode) {
    push(registers_.accumulator);
}

void CPU::op_php(AddressingMode) {
    push_status();
}

void CPU::op_pla(AddressingMode) {
    set_register(registers_.accumulator, pull());
}

void CPU::op_plp(AddressingMode) {
    pull_status();
}

// --- Arithmetic and logic ---

void CPU::op_adc(AddressingMode mode) {
    add_with_carry(read_operand(mode));
}

void CPU::op_sbc(AddressingMode mode) {
    subtract_with_carry(read_operand(mode));
}

void CPU::op_and(AddressingMode mode) {
    set_register(registers_.accumulator, registers_.accumulator & read_operand(mode));
}

void CPU::op_ora(AddressingMode mode) {
    set_register(registers_.accumulator, registers_.accumulator | read_operand(mode));
}

void CPU::op_eor(AddressingMode mode) {
    set_register(registers_.accumulator, registers_.accumulator ^ read_operand(mode));
}

// BIT tests memory against A without changing either. Zero reflects A AND
// memory; Negative and Overflow are copied straight from bits 7 and 6 of the
// memory value.
void CPU::op_bit(AddressingMode mode) {
    const uint8_t value = read_operand(mode);
    set_flag(Zero, (registers_.accumulator & value) == 0);
    set_flag(Negative, (value & 0x80) != 0);
    set_flag(Overflow, (value & 0x40) != 0);
}

void CPU::op_cmp(AddressingMode mode) {
    compare(registers_.accumulator, read_operand(mode));
}

void CPU::op_cpx(AddressingMode mode) {
    compare(registers_.x, read_operand(mode));
}

void CPU::op_cpy(AddressingMode mode) {
    compare(registers_.y, read_operand(mode));
}

// --- Increment and decrement ---

void CPU::op_inc(AddressingMode mode) {
    modify_operand(mode, &CPU::increment);
}

void CPU::op_dec(AddressingMode mode) {
    modify_operand(mode, &CPU::decrement);
}

void CPU::op_inx(AddressingMode) {
    registers_.x = increment(registers_.x);
}

void CPU::op_iny(AddressingMode) {
    registers_.y = increment(registers_.y);
}

void CPU::op_dex(AddressingMode) {
    registers_.x = decrement(registers_.x);
}

void CPU::op_dey(AddressingMode) {
    registers_.y = decrement(registers_.y);
}

// --- Shifts and rotates ---

void CPU::op_asl(AddressingMode mode) {
    modify_operand(mode, &CPU::shift_left);
}

void CPU::op_lsr(AddressingMode mode) {
    modify_operand(mode, &CPU::shift_right);
}

void CPU::op_rol(AddressingMode mode) {
    modify_operand(mode, &CPU::rotate_left);
}

void CPU::op_ror(AddressingMode mode) {
    modify_operand(mode, &CPU::rotate_right);
}

// --- Branches ---

void CPU::op_bpl(AddressingMode) {
    branch_if(!flag(Negative));
}

void CPU::op_bmi(AddressingMode) {
    branch_if(flag(Negative));
}

void CPU::op_bvc(AddressingMode) {
    branch_if(!flag(Overflow));
}

void CPU::op_bvs(AddressingMode) {
    branch_if(flag(Overflow));
}

void CPU::op_bcc(AddressingMode) {
    branch_if(!flag(Carry));
}

void CPU::op_bcs(AddressingMode) {
    branch_if(flag(Carry));
}

void CPU::op_bne(AddressingMode) {
    branch_if(!flag(Zero));
}

void CPU::op_beq(AddressingMode) {
    branch_if(flag(Zero));
}

// --- Jumps, subroutines and interrupts ---

void CPU::op_jmp(AddressingMode mode) {
    registers_.program_counter = operand_address(mode);
}

// JSR pushes the address of its own last byte, not of the next instruction.
// RTS adds the missing one back.
void CPU::op_jsr(AddressingMode) {
    const uint16_t target = fetch_word();
    push_word(registers_.program_counter - 1);
    registers_.program_counter = target;
}

void CPU::op_rts(AddressingMode) {
    registers_.program_counter = pull_word() + 1;
}

// BRK is a software interrupt. It skips the byte after the opcode, saves the
// return address and status, disables further interrupts, and jumps through
// the vector at $FFFE.
void CPU::op_brk(AddressingMode) {
    registers_.program_counter++;
    push_word(registers_.program_counter);
    push_status();
    set_flag(InterruptDisable, true);
    registers_.program_counter = read_word(kInterruptVector);
}

void CPU::op_rti(AddressingMode) {
    pull_status();
    registers_.program_counter = pull_word();
}

// The undocumented NOPs take an operand, which still has to be skipped.
void CPU::op_nop(AddressingMode mode) {
    if (mode != Implied) {
        operand_address(mode);
    }
}

// --- Flag changes ---

void CPU::op_clc(AddressingMode) {
    set_flag(Carry, false);
}

void CPU::op_sec(AddressingMode) {
    set_flag(Carry, true);
}

void CPU::op_cli(AddressingMode) {
    set_flag(InterruptDisable, false);
}

void CPU::op_sei(AddressingMode) {
    set_flag(InterruptDisable, true);
}

void CPU::op_cld(AddressingMode) {
    set_flag(Decimal, false);
}

void CPU::op_sed(AddressingMode) {
    set_flag(Decimal, true);
}

void CPU::op_clv(AddressingMode) {
    set_flag(Overflow, false);
}

// ---------------------------------------------------------------------------
// Undocumented instructions
//
// These opcodes were never part of the official instruction set. They fall out
// of how the chip decodes opcodes, and most behave like two documented
// instructions running at once. Some software relies on them.
// ---------------------------------------------------------------------------

// SLO: ASL memory, then ORA the result into A.
void CPU::op_slo(AddressingMode mode) {
    const uint8_t shifted = modify_operand(mode, &CPU::shift_left);
    set_register(registers_.accumulator, registers_.accumulator | shifted);
}

// RLA: ROL memory, then AND the result into A.
void CPU::op_rla(AddressingMode mode) {
    const uint8_t rotated = modify_operand(mode, &CPU::rotate_left);
    set_register(registers_.accumulator, registers_.accumulator & rotated);
}

// SRE: LSR memory, then EOR the result into A.
void CPU::op_sre(AddressingMode mode) {
    const uint8_t shifted = modify_operand(mode, &CPU::shift_right);
    set_register(registers_.accumulator, registers_.accumulator ^ shifted);
}

// RRA: ROR memory, then ADC the result to A.
void CPU::op_rra(AddressingMode mode) {
    add_with_carry(modify_operand(mode, &CPU::rotate_right));
}

// DCP: DEC memory, then CMP it with A.
void CPU::op_dcp(AddressingMode mode) {
    compare(registers_.accumulator, modify_operand(mode, &CPU::decrement));
}

// ISC: INC memory, then SBC it from A.
void CPU::op_isc(AddressingMode mode) {
    subtract_with_carry(modify_operand(mode, &CPU::increment));
}

// LAX: load A and X with the same value.
void CPU::op_lax(AddressingMode mode) {
    set_register(registers_.accumulator, read_operand(mode));
    registers_.x = registers_.accumulator;
}

// SAX: store A AND X.
void CPU::op_sax(AddressingMode mode) {
    write(operand_address(mode), registers_.accumulator & registers_.x);
}

// ANC: AND, then copy the sign bit into Carry.
void CPU::op_anc(AddressingMode mode) {
    set_register(registers_.accumulator, registers_.accumulator & read_operand(mode));
    set_flag(Carry, flag(Negative));
}

// ALR: AND, then LSR A.
void CPU::op_alr(AddressingMode mode) {
    registers_.accumulator = shift_right(registers_.accumulator & read_operand(mode));
}

// ARR: AND, then ROR A, but with Carry and Overflow taken from bits 6 and 5
// of the result instead of the bit shifted out.
void CPU::op_arr(AddressingMode mode) {
    const uint8_t masked = registers_.accumulator & read_operand(mode);
    const uint8_t carry_in = flag(Carry) ? 0x80 : 0x00;
    set_register(registers_.accumulator, static_cast<uint8_t>(masked >> 1 | carry_in));

    const bool bit6 = (registers_.accumulator & 0x40) != 0;
    const bool bit5 = (registers_.accumulator & 0x20) != 0;
    set_flag(Carry, bit6);
    set_flag(Overflow, bit6 != bit5);
}

// ANE and LXA are unstable on real hardware: the result depends on a "magic"
// constant that varies between chips. $EE is the commonly observed value.
void CPU::op_ane(AddressingMode mode) {
    set_register(registers_.accumulator, (registers_.accumulator | 0xEE) & registers_.x & read_operand(mode));
}

void CPU::op_lxa(AddressingMode mode) {
    set_register(registers_.accumulator, (registers_.accumulator | 0xEE) & read_operand(mode));
    registers_.x = registers_.accumulator;
}

// SBX: X = (A AND X) - operand, setting flags like CMP.
void CPU::op_sbx(AddressingMode mode) {
    const uint8_t operand = read_operand(mode);
    const uint8_t combined = registers_.accumulator & registers_.x;
    set_flag(Carry, combined >= operand);
    set_register(registers_.x, static_cast<uint8_t>(combined - operand));
}

// LAS: AND memory with the stack pointer and load the result into A, X and SP.
void CPU::op_las(AddressingMode mode) {
    set_register(registers_.accumulator, read_operand(mode) & registers_.stack_pointer);
    registers_.x = registers_.accumulator;
    registers_.stack_pointer = registers_.accumulator;
}

// SHA, SHX, SHY and SHS store registers ANDed with the high byte of the
// target address plus one.
void CPU::op_sha(AddressingMode mode) {
    const uint16_t address = operand_address(mode);
    write(address, registers_.accumulator & registers_.x & high_byte_plus_one(address));
}

void CPU::op_shx(AddressingMode mode) {
    const uint16_t address = operand_address(mode);
    write(address, registers_.x & high_byte_plus_one(address));
}

void CPU::op_shy(AddressingMode mode) {
    const uint16_t address = operand_address(mode);
    write(address, registers_.y & high_byte_plus_one(address));
}

// SHS also copies A AND X into the stack pointer.
void CPU::op_shs(AddressingMode mode) {
    const uint16_t address = operand_address(mode);
    registers_.stack_pointer = registers_.accumulator & registers_.x;
    write(address, registers_.stack_pointer & high_byte_plus_one(address));
}

// On real hardware a JAM opcode freezes the CPU until it is reset. Throwing
// makes the fault visible instead of hanging the emulator.
void CPU::op_jam(AddressingMode) {
    throw std::runtime_error("CPU executed a JAM opcode and halted");
}

}  // namespace emulator
