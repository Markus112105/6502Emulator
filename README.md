# 6502 Emulator

6502 Emulator is a cycle-accurate software implementation of the MOS Technology 6502, the CPU that powered platforms such as the NES, Commodore 64, and Apple II. The project emphasises clarity and instructional value while modelling instruction decoding, processor state, addressing modes, and timing behaviour in modern C++.

---

## Table of Contents
- [Project Goals](#project-goals)
- [Key Features](#key-features)
- [Architecture Overview](#architecture-overview)
- [Core Data Structures](#core-data-structures)
- [Build and Run](#build-and-run)
- [Testing](#testing)
- [Project Layout](#project-layout)
- [Roadmap](#roadmap)
- [Further Reading](#further-reading)

---

## Project Goals
- Deliver a faithful, cycle-accurate model of the 6502 CPU.
- Provide an approachable codebase for learning CPU and emulator design.
- Separate functional correctness from optional performance optimisations.
- Supply documentation and tests that explain implementation trade-offs.

## Key Features
- **Instruction Set Coverage:** Implement all 56 documented instructions across 151 opcodes.
- **Addressing Modes:** Support every addressing mode with accurate side effects and cycle counts.
- **Timing Model:** Track per-instruction cycle usage to reflect pipeline and memory latency.
- **Memory Bus:** Represent the zero page, stack, and program memory through a dedicated bus abstraction.
- **Decimal & Undocumented Behaviour:** Model BCD arithmetic for `ADC`/`SBC` and surface undocumented opcodes, trapping on `JAM` for safe debugging.
- **Debug Tooling (Planned):** Provide a disassembler and interactive stepping environment for exploration.

## Architecture Overview
The emulator is organised around dedicated modules that mirror hardware responsibilities:
- `cpu.hpp` / `cpu.cpp` implement registers, status flags, and the instruction execution loop.
- `bus.hpp` / `bus.cpp` route reads and writes between the CPU, RAM, ROM, and peripherals.
- `main.cpp` initialises the emulator, loads binary programs, and drives the execution cycle.

Supporting modules will capture addressing logic, instruction metadata, and optional debugging interfaces as the project evolves.

## Core Data Structures
- **Registers:** `A`, `X`, `Y`, `SP`, `PC`, and a packed status register track CPU state.
- **Memory:** A contiguous 64 KiB space (`std::array<uint8_t, 0x10000>`) models the address bus.
- **Instruction Table:** A dispatch table links each opcode to its handler, addressing mode, and cycle cost.

## Build and Run
The project targets C++20 and currently supports two build paths.

### Using CMake (recommended)
```bash
mkdir -p build
cd build
cmake ..
cmake --build .
./emulator path/to/test_program.bin [--start 0x8000] [--max-cycles 1000000] [--trace]
```

`--start` selects the load/reset vector address (default `0x8000`), `--max-cycles` bounds execution to avoid infinite loops, and `--trace` prints each opcode as it executes. The emulator halts automatically when it encounters a `BRK`.

### Using g++ directly
```bash
g++ -std=c++20 -O2 src/main.cpp src/cpu.cpp src/bus.cpp -o emulator
./emulator path/to/test_program.bin
```

### Sample Console Output
```
[Cycle 0x0001] LDA #$01  => A = 0x01
[Cycle 0x0002] STA $0200 => MEM[0200] = 0x01
[Cycle 0x0003] BRK       => Halt
```

## Testing
Unit and integration tests use the lightweight Catch2 harness bundled in `tests/catch2`. Run the suite with:
```bash
cmake --build build --target tests
ctest --test-dir build
```
The suite covers instruction semantics, decimal-mode arithmetic, and cycle timing. For extended validation suites (e.g., Klaus Dormann's functional ROM), load the binary with the CLI, record the expected halt condition, and add a dedicated test fixture in `tests/` when you are ready to automate it.

## Project Layout
```
6502emulator/
├── src/
│   ├── bus.cpp
│   ├── bus.hpp
│   ├── cpu.cpp
│   ├── cpu.hpp
│   └── main.cpp
├── tests/
│   ├── cpu_tests.cpp
│   ├── integration_timing_tests.cpp
│   ├── catch2/
│   │   └── catch.hpp
│   └── support/
│       └── cpu_harness.hpp
├── docs/
│   ├── AGENTS.md
│   └── DESIGN_NOTES.md
└── README.md
```

## Roadmap
- Implement the instruction decoder and opcode handlers.
- Model each addressing mode and shared micro-operations.
- Calibrate cycle timings against documented hardware behaviour.
- Introduce a disassembler and single-step debugging console.
- Explore optional front-ends such as ImGui timelines or NES cartridge loaders.

## Further Reading
- `docs/AGENTS.md` describes contributor workflows and coding conventions.
- `docs/DESIGN_NOTES.md` captures architectural decisions and future enhancements.
- [_Programming the 6502_ by Rodnay Zaks](https://archive.org/details/Programming_the_6502) for background on the original processor.
