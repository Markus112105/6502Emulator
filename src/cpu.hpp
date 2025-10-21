#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace emulator {

class Bus;

// CPU models the functional state of the 6502 core and cooperates with a Bus
// instance to perform memory transactions. The class owns register state and an
// instruction dispatch table so that execution logic is centralised and easy to
// reason about while the Bus remains responsible for memory semantics.
class CPU {
public:
    enum class StatusFlag : uint8_t {
        Carry = 0x01,
        Zero = 0x02,
        InterruptDisable = 0x04,
        Decimal = 0x08,
        Break = 0x10,
        Unused = 0x20,
        Overflow = 0x40,
        Negative = 0x80,
    };

    struct Registers {
        uint8_t accumulator = 0;
        uint8_t x = 0;
        uint8_t y = 0;
        uint8_t stack_pointer = 0xFD;
        uint16_t program_counter = 0;
        uint8_t status = 0x34;
    };

    CPU();

    void attach_bus(Bus* bus);
    void reset();
    void step();

    // Tests and debugger tooling can interrogate CPU state without exposing
    // mutating access. The structure remains small enough to pass by value, but
    // we return references to avoid accidental copies in hot paths.
    const Registers& registers() const { return registers_; }

    uint64_t cycles() const { return cycles_; }

private:

    struct InstructionDescriptor {
        enum class AddressingMode {
            Accumulator,
            Immediate,
            Implied,
            Relative,
            ZeroPage,
            ZeroPageX,
            ZeroPageY,
            Absolute,
            AbsoluteX,
            AbsoluteY,
            Indirect,
            IndexedIndirect,
            IndirectIndexed,
        };

        using Handler = void (CPU::*)(AddressingMode);
        Handler handler = nullptr;
        AddressingMode mode = AddressingMode::Implied;
        uint8_t cycles = 0;
        bool page_cross_cycle = false;
    };

    using AddressingMode = InstructionDescriptor::AddressingMode;

    void build_dispatch_table();
    void execute_instruction(uint8_t opcode);
    void illegal_instruction(uint8_t opcode);

    uint8_t fetch_byte();
    uint16_t fetch_word();
    uint8_t read(uint16_t address) const;
    void write(uint16_t address, uint8_t value);

    void push(uint8_t value);
    void push16(uint16_t value);
    uint8_t pull();
    uint16_t pull16();

    void consume_operand(AddressingMode mode);
    void adc(uint8_t operand);
    void sbc(uint8_t operand);

    uint16_t compute_address(AddressingMode mode, bool& page_crossed);
    uint8_t read_operand(AddressingMode mode, bool& page_crossed);

    bool flag(StatusFlag flag) const;
    void set_flag(StatusFlag flag, bool value);
    void update_zero_and_negative(uint8_t value);

    void op_adc(AddressingMode mode);
    void op_and(AddressingMode mode);
    void op_asl(AddressingMode mode);
    void op_branch(AddressingMode mode, bool condition);
    void op_bit(AddressingMode mode);
    void op_brk(AddressingMode mode);
    void op_clc(AddressingMode mode);
    void op_cld(AddressingMode mode);
    void op_cli(AddressingMode mode);
    void op_clv(AddressingMode mode);
    void op_cmp(AddressingMode mode);
    void op_cpx(AddressingMode mode);
    void op_cpy(AddressingMode mode);
    void op_dec(AddressingMode mode);
    void op_dex(AddressingMode mode);
    void op_dey(AddressingMode mode);
    void op_eor(AddressingMode mode);
    void op_inc(AddressingMode mode);
    void op_inx(AddressingMode mode);
    void op_iny(AddressingMode mode);
    void op_jmp(AddressingMode mode);
    void op_jsr(AddressingMode mode);
    void op_lda(AddressingMode mode);
    void op_ldx(AddressingMode mode);
    void op_ldy(AddressingMode mode);
    void op_lsr(AddressingMode mode);
    void op_nop(AddressingMode mode);
    void op_ora(AddressingMode mode);
    void op_pha(AddressingMode mode);
    void op_php(AddressingMode mode);
    void op_pla(AddressingMode mode);
    void op_plp(AddressingMode mode);
    void op_rol(AddressingMode mode);
    void op_ror(AddressingMode mode);
    void op_rti(AddressingMode mode);
    void op_rts(AddressingMode mode);
    void op_sbc(AddressingMode mode);
    void op_sec(AddressingMode mode);
    void op_sed(AddressingMode mode);
    void op_sei(AddressingMode mode);
    void op_sta(AddressingMode mode);
    void op_stx(AddressingMode mode);
    void op_sty(AddressingMode mode);
    void op_tax(AddressingMode mode);
    void op_tay(AddressingMode mode);
    void op_tsx(AddressingMode mode);
    void op_txa(AddressingMode mode);
    void op_txs(AddressingMode mode);
    void op_tya(AddressingMode mode);

    void op_bpl(AddressingMode mode);
    void op_bmi(AddressingMode mode);
    void op_bvc(AddressingMode mode);
    void op_bvs(AddressingMode mode);
    void op_bcc(AddressingMode mode);
    void op_bcs(AddressingMode mode);
    void op_bne(AddressingMode mode);
    void op_beq(AddressingMode mode);

    void op_lax(AddressingMode mode);
    void op_sax(AddressingMode mode);
    void op_dcp(AddressingMode mode);
    void op_isc(AddressingMode mode);
    void op_slo(AddressingMode mode);
    void op_rla(AddressingMode mode);
    void op_sre(AddressingMode mode);
    void op_rra(AddressingMode mode);
    void op_anc(AddressingMode mode);
    void op_alr(AddressingMode mode);
    void op_arr(AddressingMode mode);
    void op_ane(AddressingMode mode);
    void op_lxa(AddressingMode mode);
    void op_las(AddressingMode mode);
    void op_sbx(AddressingMode mode);
    void op_sha(AddressingMode mode);
    void op_shs(AddressingMode mode);
    void op_shx(AddressingMode mode);
    void op_shy(AddressingMode mode);
    void op_jam(AddressingMode mode);

    Registers registers_{};
    Bus* bus_ = nullptr;
    std::array<InstructionDescriptor, 256> dispatch_table_{};
    uint64_t cycles_ = 0;
    const InstructionDescriptor* active_instruction_ = nullptr;
    bool page_crossed_ = false;
};

}  // namespace emulator
