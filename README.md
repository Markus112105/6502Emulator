# 6502 Emulator

[![CI](https://github.com/Markus112105/6502Emulator/actions/workflows/ci.yml/badge.svg)](https://github.com/Markus112105/6502Emulator/actions/workflows/ci.yml)

An emulator for the MOS 6502, the 8-bit CPU inside the Apple II, Commodore 64, and NES, written from scratch in C++20 with no dependencies.

It passes [Klaus Dormann's 6502 functional test](https://github.com/Klaus2m5/6502_65C02_functional_tests), a ROM that checks every documented opcode in every addressing mode, including decimal-mode arithmetic:

```
$ make functional
Trapped at $3469 after 30646177 instructions, 96241367 cycles
PASS: Klaus Dormann 6502 functional test
```

## What it does

- All 151 documented opcodes (56 instructions, 13 addressing modes).
- The undocumented NMOS opcodes as well (`LAX`, `SAX`, `DCP`, `ISC`, `SLO`, `RLA`, `SRE`, `RRA`, and the rest). The twelve `JAM` opcodes raise an error instead of locking up.
- Cycle counting per instruction, including the extra cycle for page crossings and for taken branches.
- BCD arithmetic for `ADC` and `SBC` when the decimal flag is set.
- Hardware quirks that real software depends on, such as the `JMP ($xxFF)` page-wrap bug and zero-page pointer wrapping.
- A command-line runner that loads a raw binary, executes it, and can trace every instruction.

## Build and run

You need a C++20 compiler (GCC 10+, Clang 12+) and `make`.

```bash
make                # builds ./emulator
make test           # unit and timing tests
make functional     # downloads the functional test ROM once, then runs it
```

Try it with a six-instruction program that stores a value and counts X down from 5:

```bash
printf '\xA9\x01\x8D\x00\x02\xA2\x05\xCA\xD0\xFD\x00' > demo.bin
./emulator demo.bin --trace
```

```
Loaded program: "demo.bin"
Start address: $8000, cycle budget: 1000000
[PC=$8000] opcode $A9
[PC=$8002] opcode $8D
[PC=$8005] opcode $A2
[PC=$8007] opcode $CA
[PC=$8008] opcode $D0
...
BRK encountered at $800A after 39 cycles.
```

Options:

| Flag | Default | Meaning |
| --- | --- | --- |
| `--start <addr>` | `0x8000` | Address the program is loaded at and started from |
| `--max-cycles <n>` | `1000000` | Stop after this many cycles if the program never reaches `BRK` |
| `--trace` | off | Print the program counter and opcode of every instruction |

The runner exits with status 0 when the program reaches `BRK`.

## How it works

The code is split the way the hardware is:

- **`src/cpu.cpp`** holds the registers and a 256-entry dispatch table. Each entry pairs an opcode with a handler, an addressing mode, a base cycle count, and whether a page crossing costs an extra cycle. `CPU::step()` fetches one opcode, looks it up, and runs the handler, so each instruction is written once and reused across its addressing modes.
- **`src/bus.cpp`** owns the 64 KiB address space. The CPU only reaches memory through the bus, which leaves room to add memory-mapped devices without touching the CPU.
- **`src/main.cpp`** is the command-line runner.

```
src/
  cpu.hpp, cpu.cpp      registers, dispatch table, instruction handlers
  bus.hpp, bus.cpp      64 KiB memory and program loading
  main.cpp              command-line runner
tests/
  cpu_tests.cpp                   instruction behaviour, flags, decimal mode
  integration_timing_tests.cpp    cycle counts and page-cross penalties
  functional_rom_test.cpp         runs the Klaus Dormann ROM
  catch2/catch.hpp                small Catch2-style test header
```

## Testing

There are two layers:

1. **Unit tests** (`make test`) assemble a few bytes of machine code, step the CPU, and check registers, flags, memory, and cycle counts.
2. **The functional test ROM** (`make functional`) runs about 30 million instructions through the core. The ROM signals a failure by jumping to itself, so the runner steps until the program counter stops moving and checks that it stopped at the success address. The ROM is GPL-licensed, so it is downloaded at a pinned commit and verified by checksum instead of being stored in this repository.

Both run on Linux and macOS in CI on every push.

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
