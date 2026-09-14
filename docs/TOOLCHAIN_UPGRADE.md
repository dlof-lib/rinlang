# Toolchain Upgrade — 2026-09

## Added
- Shared semantic-analysis layer over the canonical Rin AST.
- Static type checker with optional annotations and Any-compatible gradual typing.
- Core bytecode instruction set and compiler.
- Optimizer with conservative constant-folding peephole pass.
- Binary `.rbc` bytecode serialization (`RBC1`).
- Bytecode VM with execution budget and runtime diagnostics.
- CLI commands: `analyze`, `bytecode`, `vm`, `toolchain-new`.
- Professional project generator output (`rin.toml`, `src`, `tests`, `build`, README).
- `docs/TOOLCHAIN.md` architecture and extension roadmap.

## Compatibility
- Existing Lexer, Parser, AST, Interpreter, `rinc`, RinPM and container systems remain the canonical implementations.
- Existing `rin run`, `rin build`, `rin check`, `rin pkg`, `rin new`, `rin fmt`, `rin test`, and `rin doctor` command paths are preserved.
- The uploaded editor crash-fix (`RinCodeEditorView.kt`) was merged unchanged from the supplied patch archive.

## Verification
- `rin_toolchain.cpp`: C++17 syntax check passed.
- `cli/linux/src/main.cpp`: C++17 syntax check passed.
- Minimal Lexer + Parser + Toolchain + VM integration test executed successfully and returned `14`.
- Full CLI CMake build was started; the repository's large existing `rin_interpreter.cpp` compilation exceeded the execution time budget before final linking. No compiler error was observed in the new toolchain sources.
