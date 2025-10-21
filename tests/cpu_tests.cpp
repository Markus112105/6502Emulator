#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include "support/cpu_harness.hpp"

namespace {

using emulator::testing::Harness;
constexpr uint8_t kCarryFlag = emulator::testing::kCarryFlag;
constexpr uint8_t kZeroFlag = emulator::testing::kZeroFlag;
constexpr uint8_t kDecimalFlag = emulator::testing::kDecimalFlag;
constexpr uint8_t kOverflowFlag = emulator::testing::kOverflowFlag;
constexpr uint8_t kNegativeFlag = emulator::testing::kNegativeFlag;

}  // namespace

TEST_CASE("CPU reset loads program counter from reset vector", "[cpu][reset]") {
    Harness harness({0xEA});  // Single NOP ensures the bus holds at least one opcode.

    REQUIRE(harness.cpu.registers().program_counter == 0x8000);
    REQUIRE((harness.cpu.registers().status & kCarryFlag) == 0);
}

TEST_CASE("LDA immediate updates accumulator and flags", "[cpu][load]") {
    Harness harness({0xA9, 0x00, 0xA9, 0x80});

    harness.step();  // LDA #$00
    REQUIRE(harness.cpu.registers().accumulator == 0x00);
    REQUIRE((harness.cpu.registers().status & kZeroFlag) != 0);
    REQUIRE((harness.cpu.registers().status & kNegativeFlag) == 0);

    harness.step();  // LDA #$80
    REQUIRE(harness.cpu.registers().accumulator == 0x80);
    REQUIRE((harness.cpu.registers().status & kZeroFlag) == 0);
    REQUIRE((harness.cpu.registers().status & kNegativeFlag) != 0);
}

TEST_CASE("ADC handles carry and overflow transitions", "[cpu][alu]") {
    Harness harness({0xA9, 0x50, 0x69, 0x50});

    harness.step();  // LDA #$50
    harness.step();  // ADC #$50 => 0x50 + 0x50 = 0xA0

    REQUIRE(harness.cpu.registers().accumulator == 0xA0);
    const uint8_t status = harness.cpu.registers().status;
    REQUIRE((status & kCarryFlag) == 0);
    REQUIRE((status & kOverflowFlag) != 0);
    REQUIRE((status & kNegativeFlag) != 0);
}

TEST_CASE("STA absolute stores accumulator into memory", "[cpu][store]") {
    Harness harness({0xA9, 0x42, 0x8D, 0x00, 0x20});

    harness.step();  // LDA #$42
    harness.step();  // STA $2000

    REQUIRE(harness.bus.read(0x2000) == 0x42);
}

TEST_CASE("Branch instructions redirect control flow when conditions match", "[cpu][branch]") {
    Harness harness({
        0xA9, 0x00,  // LDA #$00 => zero flag set
        0xF0, 0x02,  // BEQ +2 => skip next LDA
        0xA9, 0x01,  // (skipped)
        0xA9, 0x05   // Target: LDA #$05
    });

    harness.step();  // LDA #$00
    harness.step();  // BEQ taken
    REQUIRE(harness.cpu.registers().program_counter == 0x8006);

    harness.step();  // Execute target instruction
    REQUIRE(harness.cpu.registers().accumulator == 0x05);
}

TEST_CASE("JSR and RTS preserve return address on the stack", "[cpu][control]") {
    Harness harness({
        0x20, 0x05, 0x80,  // JSR $8005
        0xA9, 0xFF,        // LDA #$FF (executed after returning)
        0xA9, 0x42,        // $8005: LDA #$42
        0x60               // RTS
    });

    harness.step();  // JSR
    REQUIRE(harness.cpu.registers().program_counter == 0x8005);

    harness.step();  // Subroutine LDA
    REQUIRE(harness.cpu.registers().accumulator == 0x42);

    harness.step();  // RTS
    REQUIRE(harness.cpu.registers().program_counter == 0x8003);
    REQUIRE(harness.cpu.registers().stack_pointer == 0xFD);

    harness.step();  // Final LDA #$FF
    REQUIRE(harness.cpu.registers().accumulator == 0xFF);
}

TEST_CASE("Undocumented read-modify-write opcodes compose predictably", "[cpu][undocumented]") {
    Harness harness({
        0xA9, 0xCF,  // LDA #$CF
        0xA2, 0x5A,  // LDX #$5A
        0x87, 0x20,  // SAX $20 => MEM[$20] = A & X
        0xA9, 0x00,  // LDA #$00
        0xA7, 0x20,  // LAX $20 => A = X = MEM[$20]
        0x07, 0x20,  // SLO $20 => ASL, ORA
        0x27, 0x20,  // RLA $20 => ROL, AND
        0x67, 0x20,  // RRA $20 => ROR, ADC
        0xE7, 0x20   // ISC $20 => INC, SBC
    });

    harness.step();  // LDA #$CF
    REQUIRE(harness.cpu.registers().accumulator == 0xCF);

    harness.step();  // LDX #$5A
    REQUIRE(harness.cpu.registers().x == 0x5A);

    harness.step();  // SAX $20
    REQUIRE(harness.bus.read(0x20) == 0x4A);
    REQUIRE(harness.cpu.registers().accumulator == 0xCF);

    harness.step();  // LDA #$00
    REQUIRE(harness.cpu.registers().accumulator == 0x00);
    REQUIRE((harness.cpu.registers().status & kZeroFlag) != 0);

    harness.step();  // LAX $20
    REQUIRE(harness.cpu.registers().accumulator == 0x4A);
    REQUIRE(harness.cpu.registers().x == 0x4A);

    harness.step();  // SLO $20
    REQUIRE(harness.bus.read(0x20) == 0x94);
    REQUIRE(harness.cpu.registers().accumulator == 0xDE);
    auto status = harness.cpu.registers().status;
    REQUIRE((status & kNegativeFlag) != 0);
    REQUIRE((status & kCarryFlag) == 0);

    harness.step();  // RLA $20
    REQUIRE(harness.bus.read(0x20) == 0x28);
    status = harness.cpu.registers().status;
    REQUIRE((status & kCarryFlag) != 0);
    REQUIRE(harness.cpu.registers().accumulator == 0x08);

    harness.step();  // RRA $20
    REQUIRE(harness.bus.read(0x20) == 0x94);
    status = harness.cpu.registers().status;
    REQUIRE((status & kCarryFlag) == 0);
    REQUIRE(harness.cpu.registers().accumulator == 0x9C);
    REQUIRE((status & kNegativeFlag) != 0);

    harness.step();  // ISC $20
    REQUIRE(harness.bus.read(0x20) == 0x95);
    status = harness.cpu.registers().status;
    REQUIRE((status & kCarryFlag) != 0);
    REQUIRE((status & kZeroFlag) == 0);
    REQUIRE(harness.cpu.registers().accumulator == 0x06);
    REQUIRE(harness.cpu.registers().x == 0x4A);
    REQUIRE(harness.cpu.cycles() == 32);
}

TEST_CASE("Indirect jump preserves classic zero-page wrapping", "[cpu][integration][memory]") {
    Harness harness({
        0x6C, 0xFF, 0x00,  // JMP ($00FF) => should wrap to ($0000)
        0xA9, 0x01,        // LDA #$01 (target)
        0x8D, 0x00, 0x02   // STA $0200
    });

    harness.bus.write(0x00FF, 0x03);  // Low byte of target
    harness.bus.write(0x0000, 0x80);  // High byte read with page wrap

    harness.step();  // JMP indirect
    REQUIRE(harness.cpu.registers().program_counter == 0x8003);

    harness.step();  // LDA #$01
    REQUIRE(harness.cpu.registers().accumulator == 0x01);

    harness.step();  // STA $0200
    REQUIRE(harness.bus.read(0x0200) == 0x01);
    REQUIRE(harness.cpu.cycles() == 11);
}

TEST_CASE("Decimal ADC and SBC honour BCD digit behaviour", "[cpu][decimal]") {
    Harness harness({
        0xF8,        // SED
        0xA9, 0x45,  // LDA #$45
        0x69, 0x55,  // ADC #$55 => 0x45 + 0x55 = 0x00 with carry
        0xE9, 0x15,  // SBC #$15 => 0x00 - 0x15 = 0x85 (borrow)
        0xE9, 0x30   // SBC #$30 => 0x85 - 0x30 - borrow = 0x54
    });

    harness.step();  // SED
    REQUIRE((harness.cpu.registers().status & kDecimalFlag) != 0);
    REQUIRE(harness.cpu.cycles() == 2);

    harness.step();  // LDA #$45
    REQUIRE(harness.cpu.registers().accumulator == 0x45);
    REQUIRE(harness.cpu.cycles() == 4);

    harness.step();  // ADC #$55
    const auto status_after_adc = harness.cpu.registers().status;
    REQUIRE(harness.cpu.registers().accumulator == 0x00);
    REQUIRE((status_after_adc & kCarryFlag) != 0);
    REQUIRE((status_after_adc & kZeroFlag) != 0);
    REQUIRE((status_after_adc & kNegativeFlag) == 0);
    REQUIRE((status_after_adc & kOverflowFlag) != 0);
    REQUIRE(harness.cpu.cycles() == 6);

    harness.step();  // SBC #$15
    const auto status_after_first_sbc = harness.cpu.registers().status;
    REQUIRE(harness.cpu.registers().accumulator == 0x85);
    REQUIRE((status_after_first_sbc & kCarryFlag) == 0);
    REQUIRE((status_after_first_sbc & kNegativeFlag) != 0);
    REQUIRE(harness.cpu.cycles() == 8);

    harness.step();  // SBC #$30
    const auto status_after_second_sbc = harness.cpu.registers().status;
    REQUIRE(harness.cpu.registers().accumulator == 0x54);
    REQUIRE((status_after_second_sbc & kCarryFlag) != 0);
    REQUIRE((status_after_second_sbc & kZeroFlag) == 0);
    REQUIRE((status_after_second_sbc & kNegativeFlag) == 0);
    REQUIRE(harness.cpu.cycles() == 10);
}
