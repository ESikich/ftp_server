# FTP Server — Design Document

Minimal, correct FTP server implementation in C23
Target protocol: RFC 959 (FTP)

---

## 1. Goals and Non-Goals

### Goals

- Correct implementation of core FTP server behavior (RFC 959)
- Conservative, predictable behavior
- Clear separation of control and data channels
- Per-session state that is explicit and easy to reason about
- No hidden allocations in hot paths
- Safe filesystem confinement under a configured export root
- Suitable as a reusable library component or small daemon backend

### Non-Goals

- FTPS / TLS support (explicit or implicit)
- Anonymous upload by default
- FXP, proxying, or server-to-server transfers
- Advanced virtual filesystem features
- Performance tuning beyond basic correctness
- Windows portability

---

## 2. Supported FTP Features (v1)

- USER / PASS authentication
- SYST, NOOP
- TYPE A / TYPE I
- PWD, CWD, CDUP
- PASV (passive mode, IPv4 only)
- LIST, NLST
- RETR (download to client)
- STOR (upload from client)
- QUIT

Unsupported commands must fail explicitly and safely with an appropriate
5xx reply.

---

## 3. Architecture Overview

Logical layers:

- Listener / Accept Loop
- Session Manager
- Transport (TCP sockets)
- Control Channel (command read and reply write)
- Command Parser (incremental, line oriented)
- Command Dispatcher
- Data Channel (passive-mode transfers)
- Filesystem Mapper (rooted path resolution)
- Authentication Layer

Each layer communicates via explicit structs. No hidden global state
except logging configuration and immutable process-wide configuration.

---

## 4. Server Process Model

- One listening TCP socket for the control port
- One session object per accepted client
- v1 uses blocking I/O per session
- Multi-client support is provided by one worker process or thread per
  session, chosen once and documented in code
- The concurrency mechanism must be simple and explicit

Assumption for v1: a process-per-session or thread-per-session model is
acceptable if it keeps control flow simple and session isolation clear.

---

## 5. Session State Model

Each client session tracks at minimum:

- Control socket
- Authentication state
- Username pending authentication, if any
- Current working directory relative to export root
- Current transfer type (`TYPE A` or `TYPE I`)
- Passive listener state, if armed
- Active data transfer state, if any
- Session timeout bookkeeping

Core states:

- Connected, greeting sent
- Awaiting authentication
- Authenticated and idle
- Passive listener armed
- Transfer in progress
- Closing

State transitions must be explicit and checked before dispatching each
command.

---

## 6. Control Connection Model

- Server sends `220` greeting after accepting a control connection
- Commands are read as CRLF-terminated lines
- Reads may return partial or multiple command lines
- Parsing is incremental
- Replies are written synchronously and fully before advancing state

Timeouts are enforced via `poll()` or socket options.

The server must reject malformed command lines conservatively and close
the session on unrecoverable protocol errors.

---

## 7. Command Parsing

- Incremental byte-fed parser
- No dynamic allocation in steady state
- Commands are ASCII verbs with optional arguments
- Command verb matching is case-insensitive
- Leading and trailing spaces in arguments are handled conservatively

Parsing output:

- Command verb
- Raw argument slice, if present
- Parse status

The parser must not assume a single `read()` corresponds to one command.

---

## 8. Reply Generation

- Replies follow RFC 959 numeric format
- Single-line replies only in v1 unless a command clearly requires
  multi-line formatting
- Reply text is conservative, stable, and implementation-oriented
- Reply codes must match actual session state and command result

Examples:

- `220` service ready
- `331` username accepted, password required
- `230` user logged in
- `221` service closing control connection
- `425` can't open data connection
- `450` requested file action not taken
- `500` syntax error, command unrecognized
- `502` command not implemented
- `530` not logged in
- `550` requested action not taken

---

## 9. Authentication Model

- v1 requires configured local credentials supplied at startup
- `USER` records the requested username
- `PASS` completes authentication only after a valid `USER`
- Commands other than a minimal pre-auth subset fail with `530`

Pre-auth allowed commands:

- USER
- PASS
- QUIT
- NOOP

Assumption for v1: authentication is against static in-memory
configuration, not PAM, not system accounts, and not anonymous access,
unless explicitly configured later.

---

## 10. Filesystem Mapping and Safety

All visible paths are mapped under a configured export root.

Rules:

- The export root is absolute and fixed at startup
- The session working directory is stored as a normalized logical path
  relative to that root
- `CWD`, `CDUP`, `LIST`, `NLST`, `RETR`, and `STOR` resolve paths only
  within that root
- Any path that would escape the root is rejected
- `..` handling must be explicit and conservative
- Symbolic link handling must be documented; if uncertain, reject or
  confine conservatively

Assumption for v1: path resolution uses canonicalization against the
export root and rejects any result outside it.

---

## 11. Data Connection Model

- Passive mode only (`PASV`)
- No active mode (`PORT`) in v1
- Each transfer uses a fresh data connection
- The passive listener lifecycle is strictly ordered:
  1. Create passive listener
  2. Advertise endpoint with `227`
  3. Receive transfer command
  4. Accept one data connection
  5. Complete transfer
  6. Close accepted socket
  7. Close passive listener

Only one passive listener may be armed per session at a time.

A new `PASV` command replaces and closes any previously armed passive
listener.

---

## 12. Directory Listing

Supported commands:

- `LIST`
- `NLST`

Behavior:

- Listing is produced over the data connection
- The server sends a 1xx preliminary reply before data transfer
- After transfer success, the server sends a 226 completion reply
- On failure, the server sends an appropriate 4xx or 5xx reply

Assumption for v1: listing format for `LIST` may be a simple
`ls -l`-like stable textual format generated internally. It need not
match every Unix FTP daemon exactly, but it must be documented and
consistent.

`NLST` returns names only, one per line.

---

## 13. File Transfers

### Download (RETR)

- Streams file contents to the data socket
- Binary safe
- Uses `TYPE I` as raw bytes
- `TYPE A` may translate local newline representation conservatively if
  implemented; otherwise document limited support

### Upload (STOR)

- Streams from the data socket into a server-side file
- Creates or truncates conservatively according to documented policy
- Partial uploads on failure are cleaned up or left in a documented,
  conservative state

Assumption for v1: `STOR` overwrites existing files only if that policy
is explicitly chosen and documented. Otherwise reject existing targets
conservatively.

---

## 14. Transfer Sequencing

For a transfer command (`LIST`, `NLST`, `RETR`, `STOR`):

1. Verify authenticated session
2. Verify valid passive listener exists
3. Validate path and requested action
4. Send preliminary 1xx reply
5. Accept data connection
6. Perform transfer
7. Close data socket
8. Send final 226 on success, or appropriate failure reply

Control and data channel logic remain strictly separated.

The control connection must remain usable after a failed data transfer
unless the failure invalidates session integrity.

---

## 15. Error Handling Strategy

- Protocol violations that break parser or session invariants are fatal
  to that session
- Listener failures during startup are fatal to the process
- Per-session I/O or filesystem errors are isolated to that session
- Data transfer failures do not corrupt control-channel state
- Cleanup paths must close all session-owned file descriptors

Reply codes must reflect the actual failure class conservatively.

---

## 16. Timeouts and Resource Limits

- Idle control sessions have a conservative timeout
- Passive listeners have a short timeout window
- Data transfers have bounded I/O wait times
- Maximum concurrent sessions is fixed in configuration
- Maximum command line length is fixed and enforced
- Maximum path length is bounded by implementation constants

On timeout, the session is closed cleanly with a best-effort final reply
when safe.

---

## 17. Logging

- Logging is process-wide and explicit
- At minimum log:
  - Listener startup and shutdown
  - Session open and close
  - Authentication success and failure
  - Transfer start and completion
  - Protocol errors
  - Filesystem access failures

Logs must not expose passwords.

---

## 18. Configuration

v1 startup configuration includes at minimum:

- Bind address
- Control port
- Export root
- Username
- Password
- Session timeout
- Passive port range

Configuration is immutable after startup.

---

## 19. Phasing Plan

1. Listener startup and greeting
2. Incremental command parser
3. Reply framework and session state machine
4. Authentication
5. Rooted path resolution and working directory
6. Passive-mode data connections
7. Directory listing
8. File download (`RETR`)
9. File upload (`STOR`)
10. Robustness, limits, and cleanup

---

## 20. Assumptions

- IPv4 only
- RFC 959 semantics dominate
- Passive mode only
- Conservative timeouts
- Static configured credentials
- Single export root
- Minimal command set only

All assumptions must be documented in code where relied upon.

---

## 21. Future Extensions (Out of Scope)

- EPSV / EPRT / IPv6
- Non-blocking or event-driven I/O
- FTPS
- Anonymous mode
- Delete / rename / mkdir / rmdir commands
- Restart/resume support
- Per-user roots or permission models
- Detailed permission and ownership emulation