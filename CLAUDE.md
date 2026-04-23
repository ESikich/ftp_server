## Role

You are implementing a minimal, correct FTP server according to the
accompanying design document (`ftp-server-design.md`). Read the design
doc fully before writing any code. When the FTP specification or the
design is silent on a detail, choose the most conservative behavior and
leave a comment documenting the assumption.

---

## Language & Standard

- Language: **C23** (`-std=c23`; GCC ≥ 13, Clang ≥ 17)
- Use C23 features only where they improve correctness or clarity
  (`[[nodiscard]]`, `constexpr`, `typeof`).
- Prefer older C idioms if they are clearer.
- Platform: **POSIX / Linux**
- `_GNU_SOURCE` is defined project-wide and must not be defined per-file.

---

## Naming Conventions

- Functions: `ftp_verb_noun()` (`ftp_cmd_handle`, `ftp_reply_send`)
- Types: `noun_t` (`ftp_session_t`, `ftp_server_t`, `slice_t`)
- Enums: `NOUN_STATE` (`SESSION_AUTH`, `DATA_LISTENING`)
- Constants: `ALL_CAPS` (`CTRL_BUF_SIZE`, `MAX_SESSIONS`)
- Parameters: short and explicit (`sess`, `srv`, `buf`, `len`)
- Locals: brief (`i`, `n`, `rc`, `fd` are acceptable)

No Hungarian notation. No redundant prefixes. Names must be readable
without a glossary.

---

## Style

- Indentation: 4 spaces, no tabs
- Braces: K&R (opening brace on the same line)
- Line length: 79 columns (hard limit)
- One blank line between logical blocks; never more than one
- Function definitions put the return type on its own line
- Pointer style: `char *p`, not `char* p`

---

## Comments

- File header: one block comment with filename and one-line purpose
- Function comments only for non-obvious contracts or invariants
- Do not narrate what the code already makes clear

---

## Error Handling

- Every system call result is checked
- No chained assignment-and-check expressions
- Prefer early returns to keep the happy path unindented
- Fatal startup errors call `ftp_fatal()` (logs to stderr and exits)
- Session or protocol errors return error codes and allow cleanup
- Per-session failures must not bring down the whole server

---

## Memory Rules

- `malloc()` / `free()` allowed only during startup, session creation,
  and shutdown
- No heap allocation in steady-state command parsing or transfer loops
- Parsers operate directly on caller-provided buffers
- Slices (`slice_t`) never own memory and are short-lived

---

## Protocol Discipline

- Never assume read boundaries match protocol boundaries
- All command parsing is incremental and state-machine driven
- FTP replies must follow RFC 959 format conservatively
- Control and data connections are strictly separated
- Session state transitions must be explicit and validated

---

## Filesystem Discipline

- All filesystem access is rooted under the configured export root
- Path resolution must prevent directory traversal
- Do not follow unvalidated client paths outside the export root
- Server-side path normalization must be conservative and explicit

---

## Concurrency Model

- Work strictly within the concurrency model defined in the design doc
- Shared server state must be minimal and explicit
- Per-session state must remain isolated
- Do not mix session control flow with listener lifecycle logic

---

## Phasing

Work strictly phase-by-phase as defined in the design document.

A phase is complete only when:
- Code compiles cleanly with `-Wall -Wextra -Wpedantic -Werror`
- Tests and manual verification for the phase succeed
- No TODOs remain for that phase
- Design deviations are documented and reflected in the design doc

---

## What Not To Do

- Do not ignore protocol violations
- Do not invent abstractions not described in the design
- Do not block indefinitely without timeouts
- Do not mix control-channel and data-channel logic
- Do not allow path escape outside the export root
- Do not keep data sockets open across transfers
- Do not optimize prematurely