#pragma once

#include <array>
#include <cstdint>

namespace emulator {

class Bus;

// CPU models the MOS 6502: its registers, its status flags, and its
// instruction set. All memory access goes through the Bus it is built with.
//
// Execution is table-driven. Each of the 256 possible opcodes has an entry in
// `instructions_` that names the handler implementing it, the addressing mode
// that locates its operand, and how many cycles it takes. step() fetches one
// opcode and runs its entry, so an instruction such as LDA is written once and
// shared by all eight of its opcodes.
class CPU {
public:
    // Bit positions of the flags packed into the status register.
    enum class StatusFlag : uint8_t {
        Carry = 0x01,
        Zero = 0x02,
        InterruptDisable = 0x04,
        Decimal = 0x08,
        Break = 0x10,
        Unused = 0x20,  // Has no meaning, but always reads back as 1.
        Overflow = 0x40,
        Negative = 0x80,
    };

    // Power-on values. The stack pointer is an offset into page $01, and
    // interrupts start out disabled.
    struct Registers {
        uint8_t accumulator = 0;
        uint8_t x = 0;
        uint8_t y = 0;
        uint8_t stack_pointer = 0xFD;
        uint16_t program_counter = 0;
        uint8_t status = 0x24;  // InterruptDisable | Unused
    };

    explicit CPU(Bus& bus);

    // Restores the power-on register values and loads the program counter
    // from the reset vector at $FFFC/$FFFD.
    void reset();

    // Executes exactly one instruction.
    void step();

    const Registers& registers() const { return registers_; }

    // Total cycles consumed since the last reset.
    uint64_t cycles() const { return cycles_; }

    // The three-letter name of the instruction an opcode decodes to.
    const char* mnemonic(uint8_t opcode) const { return instructions_[opcode].mnemonic; }

private:
    // How an instruction finds its operand. The names follow the standard
    // 6502 documentation.
    enum class AddressingMode {
        Implied,          // No operand:                CLC
        Accumulator,      // Operates on A:             ASL A
        Immediate,        // Operand is the next byte:  LDA #$10
        Relative,         // Signed branch offset:      BNE label
        ZeroPage,         // Address in page zero:      LDA $10
        ZeroPageX,        //   ... plus X:              LDA $10,X
        ZeroPageY,        //   ... plus Y:              LDX $10,Y
        Absolute,         // Full 16-bit address:       LDA $1234
        AbsoluteX,        //   ... plus X:              LDA $1234,X
        AbsoluteY,        //   ... plus Y:              LDA $1234,Y
        Indirect,         // Address stored in memory:  JMP ($1234)
        IndexedIndirect,  // Pointer at (zp + X):       LDA ($10,X)
        IndirectIndexed,  // Pointer at zp, plus Y:     LDA ($10),Y
    };

    // Lets member functions write `Carry` and `Immediate` without the enum
    // name in front.
    using enum StatusFlag;
    using enum AddressingMode;

    using Handler = void (CPU::*)(AddressingMode);

    // One row of the opcode table.
    struct Instruction {
        const char* mnemonic = nullptr;
        Handler handler = nullptr;
        AddressingMode mode = Implied;
        uint8_t cycles = 0;
        bool page_cross_penalty = false;  // One extra cycle if indexing crosses a page.
    };

    void build_instruction_table();

    // --- Memory access ---
    uint8_t read(uint16_t address) const;
    void write(uint16_t address, uint8_t value);
    uint8_t fetch_byte();   // Reads the byte at the program counter and advances it.
    uint16_t fetch_word();  // Same for a little-endian 16-bit value.
    uint16_t read_word(uint16_t pointer) const;

    // --- Stack ---
    void push(uint8_t value);
    void push_word(uint16_t value);
    uint8_t pull();
    uint16_t pull_word();
    void push_status();
    void pull_status();

    // --- Operands ---
    uint16_t offset_address(uint16_t base, int offset);
    uint16_t operand_address(AddressingMode mode);
    uint8_t read_operand(AddressingMode mode);
    uint8_t modify_operand(AddressingMode mode, uint8_t (CPU::*operation)(uint8_t));

    // --- Flags ---
    bool flag(StatusFlag flag) const;
    void set_flag(StatusFlag flag, bool value);
    void update_zero_and_negative(uint8_t value);
    void set_register(uint8_t& target, uint8_t value);

    // --- Arithmetic shared by several instructions ---
    void add_with_carry(uint8_t operand);
    void subtract_with_carry(uint8_t operand);
    void compare(uint8_t register_value, uint8_t operand);
    void branch_if(bool condition);
    uint8_t shift_left(uint8_t value);
    uint8_t shift_right(uint8_t value);
    uint8_t rotate_left(uint8_t value);
    uint8_t rotate_right(uint8_t value);
    uint8_t increment(uint8_t value);
    uint8_t decrement(uint8_t value);

    // --- Documented instructions ---
    void op_lda(AddressingMode mode);
    void op_ldx(AddressingMode mode);
    void op_ldy(AddressingMode mode);
    void op_sta(AddressingMode mode);
    void op_stx(AddressingMode mode);
    void op_sty(AddressingMode mode);

    void op_tax(AddressingMode mode);
    void op_tay(AddressingMode mode);
    void op_txa(AddressingMode mode);
    void op_tya(AddressingMode mode);
    void op_tsx(AddressingMode mode);
    void op_txs(AddressingMode mode);

    void op_pha(AddressingMode mode);
    void op_php(AddressingMode mode);
    void op_pla(AddressingMode mode);
    void op_plp(AddressingMode mode);

    void op_adc(AddressingMode mode);
    void op_sbc(AddressingMode mode);
    void op_and(AddressingMode mode);
    void op_ora(AddressingMode mode);
    void op_eor(AddressingMode mode);
    void op_bit(AddressingMode mode);
    void op_cmp(AddressingMode mode);
    void op_cpx(AddressingMode mode);
    void op_cpy(AddressingMode mode);

    void op_inc(AddressingMode mode);
    void op_dec(AddressingMode mode);
    void op_inx(AddressingMode mode);
    void op_iny(AddressingMode mode);
    void op_dex(AddressingMode mode);
    void op_dey(AddressingMode mode);

    void op_asl(AddressingMode mode);
    void op_lsr(AddressingMode mode);
    void op_rol(AddressingMode mode);
    void op_ror(AddressingMode mode);

    void op_bpl(AddressingMode mode);
    void op_bmi(AddressingMode mode);
    void op_bvc(AddressingMode mode);
    void op_bvs(AddressingMode mode);
    void op_bcc(AddressingMode mode);
    void op_bcs(AddressingMode mode);
    void op_bne(AddressingMode mode);
    void op_beq(AddressingMode mode);

    void op_jmp(AddressingMode mode);
    void op_jsr(AddressingMode mode);
    void op_rts(AddressingMode mode);
    void op_brk(AddressingMode mode);
    void op_rti(AddressingMode mode);
    void op_nop(AddressingMode mode);

    void op_clc(AddressingMode mode);
    void op_sec(AddressingMode mode);
    void op_cli(AddressingMode mode);
    void op_sei(AddressingMode mode);
    void op_cld(AddressingMode mode);
    void op_sed(AddressingMode mode);
    void op_clv(AddressingMode mode);

    // --- Undocumented instructions ---
    void op_slo(AddressingMode mode);
    void op_rla(AddressingMode mode);
    void op_sre(AddressingMode mode);
    void op_rra(AddressingMode mode);
    void op_dcp(AddressingMode mode);
    void op_isc(AddressingMode mode);
    void op_lax(AddressingMode mode);
    void op_sax(AddressingMode mode);
    void op_anc(AddressingMode mode);
    void op_alr(AddressingMode mode);
    void op_arr(AddressingMode mode);
    void op_ane(AddressingMode mode);
    void op_lxa(AddressingMode mode);
    void op_sbx(AddressingMode mode);
    void op_las(AddressingMode mode);
    void op_sha(AddressingMode mode);
    void op_shs(AddressingMode mode);
    void op_shx(AddressingMode mode);
    void op_shy(AddressingMode mode);
    void op_jam(AddressingMode mode);

    Bus& bus_;
    Registers registers_{};
    std::array<Instruction, 256> instructions_{};
    uint64_t cycles_ = 0;
    bool page_crossed_ = false;  // Set while executing if indexing crossed a page.
};

}  // namespace emulator
