# AGENTS.md — classic m2dev client source

Read `H:\m2dev-client\AGENTS.md` and
`H:\m2dev-client\m2dev-docs\docs\specifications\development-workflow.md` first.

## Scope and authority

- This repository owns the classic client C++, network phases, render engine and
  C++↔Python bindings. `src/UserInterface` is the primary client parity reference.
- Python UI, pack files, locale and assets belong to `m2dev-client`.
- Server packet values and authoritative gameplay belong to `m2dev-server-src`.

## Working rules

- Preserve the heavily dirty DX11 worktree; stage only explicitly named files.
- Before changing behavior, trace the Python call/callback, C++ binding, manager,
  network sender/handler and server counterpart.
- Never copy constants from old forks. Confirm them in this branch and current
  server sources.
- For wire structs confirm packing and offsets. For parsed DTOs use bounded field
  reads; do not `memcpy` wire bytes into naturally aligned types.
- Changes affecting runtime Python, pack, locale, proto or assets must be mirrored
  in `m2dev-client` and tested with the matching executable.

## Validation

```powershell
cmake -S . -B build
cmake --build build --config RelWithDebInfo --target UserInterface
cmake --build build --config RelWithDebInfo --target DumpProto PackMaker
cmake --build build --config RelWithDebInfo --target dx11_strict_gate
```

There is no first-party automated suite: treat this as build validation. Build
the narrowest affected target first, then `UserInterface` for client-facing
changes. Run the classic client and inspect `syserr.txt`. Network changes require
binary fixtures, server parity and UE5 coverage review. Use an atomic commit on
the current branch and link the shared WORKLOG ID.
