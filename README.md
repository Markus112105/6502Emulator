# 6502 Emulator

## What is a 6502?

The 6502 is a computer chip from 1975. A processor chip is the part of a computer that does the actual work: it reads a list of very simple instructions, such as "add these two numbers" or "store this value", and carries them out one at a time. The 6502 did this at a fraction of the price of its competitors, which made home computers and game consoles affordable. It ran the Apple II, the Commodore 64, and the original Nintendo. This project is an emulator: a program that imitates the chip so exactly that software written for a real 6502 runs on a modern computer without knowing the difference.

## Overview

A MOS 6502 emulator written from scratch in C++20, with no dependencies.

It passes [Klaus Dormann's 6502 functional test](https://github.com/Klaus2m5/6502_65C02_functional_tests), a ROM that checks every documented opcode in every addressing mode, including decimal-mode arithmetic:

```
$ make functional
Trapped at $3469 after 30646177 instructions, 96241367 cycles
PASS: Klaus Dormann 6502 functional test
```

## What it does

- All 151 documented opcodes (56 instructions, 13 addressing modes).
- The undocumented NMOS opcodes (`LAX`, `SAX`, `DCP`, `ISC`, `SLO`, `RLA`, `SRE`, `RRA`, and the rest). The twelve `JAM` opcodes raise an error instead of locking up the CPU.
- Cycle counts for every instruction, including the extra cycles for page crossings and taken branches.
- BCD arithmetic for `ADC` and `SBC` when the decimal flag is set.
- Hardware quirks that real software depends on, such as the `JMP ($xxFF)` page-wrap bug and zero-page pointer wrapping.
- A command-line runner that loads a raw binary, executes it, and can trace every instruction.

## Build and run

You need a C++20 compiler (GCC 11+, Clang 13+) and `make`.

```bash
make                # builds ./emulator
make test           # unit and timing tests
make functional     # downloads the functional test ROM once, then runs it
```

To try it, write a short program by hand. This one stores 1 at `$0200`, counts X down from 5, and stops:

```asm
      LDA #$01
      STA $0200
      LDX #$05
loop: DEX
      BNE loop
      BRK
```

```bash
printf '\xA9\x01\x8D\x00\x02\xA2\x05\xCA\xD0\xFD\x00' > demo.bin
./emulator demo.bin --trace
```

```
Loaded program: "demo.bin"
Start address: $8000, cycle budget: 1000000
$8000  LDA  A=01 X=00 Y=00 SP=FD P=24  cycles=2
$8002  STA  A=01 X=00 Y=00 SP=FD P=24  cycles=6
$8005  LDX  A=01 X=05 Y=00 SP=FD P=24  cycles=8
$8007  DEX  A=01 X=04 Y=00 SP=FD P=24  cycles=10
$8008  BNE  A=01 X=04 Y=00 SP=FD P=24  cycles=13
...
$800A  BRK  A=01 X=00 Y=00 SP=FA P=26  cycles=39
BRK encountered at $800A after 39 cycles.
```

| Flag | Default | Meaning |
| --- | --- | --- |
| `--start <addr>` | `0x8000` | Address the program is loaded at and started from |
| `--max-cycles <n>` | `1000000` | Stop after this many cycles if the program never reaches `BRK` |
| `--trace` | off | Print each instruction with the register values and cycle count after it runs |

The runner stops at the first `BRK` and exits with status 0.

## How it works

- **`src/cpu.cpp`** is table-driven. A 256-entry table gives every opcode a handler, an addressing mode, a cycle count, and whether crossing a page costs an extra cycle. `CPU::step()` fetches one opcode, looks it up, and runs the handler.
- Each instruction is written once. `LDA` has eight opcodes, one per addressing mode, but a single handler: `operand_address()` works out where the operand lives, and the handler does the rest. The shifts, rotates, increments, and decrements share one read-modify-write helper, which the undocumented opcodes reuse (`SLO` is a shift followed by an OR).
- **`src/bus.cpp`** owns the 64 KiB address space. The CPU reaches memory only through the bus, so memory-mapped devices can be added later without changing the CPU.
- **`src/main.cpp`** is the command-line runner.

```
src/
  cpu.hpp, cpu.cpp      registers, opcode table, instruction handlers
  bus.hpp, bus.cpp      64 KiB memory and program loading
  main.cpp              command-line runner
tests/
  cpu_tests.cpp                   instruction behaviour, flags, decimal mode
  integration_timing_tests.cpp    cycle counts and page-cross penalties
  functional_rom_test.cpp         runs the Klaus Dormann ROM
  support/                        test harness and a minimal test framework
```

Two design choices worth noting:

- **Decimal mode.** `ADC` and `SBC` always compute the plain binary result first, because the overflow flag is defined by it. When the decimal flag is set, a second step corrects each 4-bit digit to produce the BCD result and the carry.
- **`JAM` opcodes.** On real hardware these twelve opcodes freeze the CPU until it is reset. Here they throw an exception, and the runner reports it and exits. A frozen emulator is hard to debug; an explicit error is not.

## Testing

1. **Unit tests** (`make test`) assemble a few bytes of machine code, step the CPU, and check registers, flags, memory, and cycle counts.
2. **The functional test ROM** (`make functional`) runs about 30 million instructions through the core. The ROM reports its result through the program counter: a failed check jumps to itself forever, and so does the final success address. The runner steps until the program counter stops moving, then checks where it stopped. The ROM is GPL-licensed, so `make` downloads it at a pinned commit and verifies its checksum instead of storing it in this repository.

## Limitations

- Interrupt lines (`IRQ`, `NMI`) are not modelled yet. `BRK` and `RTI` work.
- Timing is counted per instruction. The individual bus cycles inside an instruction (dummy reads and writes) are not emulated.
- The undocumented opcodes are covered by a handful of unit tests, not by a full test ROM.

## References

- [Masswerk 6502 instruction set reference](https://www.masswerk.at/6502/6502_instruction_set.html)
- [_Programming the 6502_ by Rodnay Zaks](https://archive.org/details/Programming_the_6502)
- [Klaus Dormann's 6502 functional tests](https://github.com/Klaus2m5/6502_65C02_functional_tests)

## License

[MIT](LICENSE)
