# 6502 Emulator Design Notes

This document tracks architectural decisions, trade-offs, and follow-up work as the emulator evolves. Update the relevant section when introducing new behaviour so the rationale stays close to the implementation.

## CPU Core
- Model instruction execution through a dispatch table defined in `CPU::build_dispatch_table` so that opcode behaviour is centralised.
- Represent registers as a dedicated `Registers` structure to keep state mutation explicit and traceable.
- Decimal mode arithmetic follows the documented BCD adjustment rules for `ADC` and `SBC`. The binary sum is calculated first to derive the overflow flag, and a post-process adjusts nibbles when decimal mode is enabled. Carry reflects the decimal carry/borrow while zero/negative always derive from the final adjusted accumulator value.
- Illegal `JAM` opcodes (`0x02`, `0x12`, …) trap by raising a runtime exception. This mirrors hardware lock-up semantics while keeping the emulator debuggable.

## Memory Bus
- Maintain a contiguous 64 KiB memory image for initial bring-up. Peripheral devices and mirroring behaviour will be layered on later.
- Surface `load_memory` to support test scenarios that require pre-populated RAM or ROM images.

## Testing Strategy
- Start with deterministic unit tests that cover individual instructions and addressing modes.
- Extend towards integration tests that run known-good 6502 binaries (e.g., Klaus Dormann's suite) once the instruction set is complete.
- Timing regression tests now verify page-cross penalties, RMW cycle counts, and decimal-mode flag behaviour to guard against future regressions.

## Open Questions
- How should undocumented opcodes be represented and validated?
- What debugging affordances (e.g., trace logs, watchpoints) provide the best learning value without overwhelming the user?
