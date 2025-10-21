#include "catch2/catch.hpp"

#include "support/cpu_harness.hpp"

using emulator::testing::Harness;
using emulator::testing::kCarryFlag;
using emulator::testing::kOverflowFlag;
using emulator::testing::kZeroFlag;

namespace {

uint64_t step_and_measure(Harness& harness) {
    const uint64_t before = harness.cpu.cycles();
    harness.cpu.step();
    return harness.cpu.cycles() - before;
}

}  // namespace

TEST_CASE("Absolute,X loads pay page-cross penalty", "[cpu][timing]") {
    Harness harness({0xA2, 0x01, 0xBD, 0xFF, 0x00, 0x00});
    harness.bus.write(0x0100, 0x7F);

    REQUIRE(step_and_measure(harness) == 2);  // LDX #$01
    REQUIRE(step_and_measure(harness) == 5);  // LDA $00FF,X (page crosses)
    REQUIRE(harness.cpu.registers().accumulator == 0x7F);
}

TEST_CASE("Page-crossing branches consume four cycles", "[cpu][timing]") {
    Harness harness({0xA9, 0x01, 0xD0, 0x7F, 0x00}, 0x80F0);

    REQUIRE(step_and_measure(harness) == 2);  // LDA #$01
    const uint64_t branch_cycles = step_and_measure(harness);
    REQUIRE(branch_cycles == 4);  // 2 base + taken + page
    REQUIRE(harness.cpu.registers().program_counter == 0x8173);
}

TEST_CASE("RMW instructions honour writeback timing", "[cpu][timing]") {
    Harness harness({0xA9, 0x80, 0x85, 0x20, 0x26, 0x20, 0x00});

    REQUIRE(step_and_measure(harness) == 2);  // LDA #$80
    REQUIRE(step_and_measure(harness) == 3);  // STA $20
    REQUIRE(step_and_measure(harness) == 5);  // ROL $20
    REQUIRE((harness.cpu.registers().status & kCarryFlag) != 0);
    REQUIRE((harness.cpu.registers().status & kZeroFlag) != 0);
}

TEST_CASE("Indirect indexed access wraps zero page pointer", "[cpu][timing]") {
    Harness harness({0xA0, 0xFF, 0xB1, 0x10, 0x00});
    harness.bus.write(0x0010, 0x10);
    harness.bus.write(0x0011, 0x80);
    harness.bus.write(0x810F, 0x3C);

    REQUIRE(step_and_measure(harness) == 2);  // LDY #$FF
    REQUIRE(step_and_measure(harness) == 6);  // LDA ($10),Y with page cross
    REQUIRE(harness.cpu.registers().accumulator == 0x3C);
}

TEST_CASE("ADC captures overflow timing edges", "[cpu][timing]") {
    Harness harness({0x18, 0xA9, 0x50, 0x69, 0x50, 0x00});

    REQUIRE(step_and_measure(harness) == 2);  // CLC
    REQUIRE(step_and_measure(harness) == 2);  // LDA #$50
    REQUIRE(step_and_measure(harness) == 2);  // ADC #$50
    const auto status = harness.cpu.registers().status;
    REQUIRE((status & kCarryFlag) == 0);
    REQUIRE((status & kOverflowFlag) != 0);
}
