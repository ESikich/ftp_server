# ftp-server

Minimal, correct FTP server in C23 (RFC 959).

## Features

- USER / PASS authentication (static configured credentials)
- SYST, NOOP, TYPE A / TYPE I
- PWD, CWD, CDUP, MKD, DELE
- PASV (passive mode, IPv4 only)
- LIST, NLST
- RETR (download), STOR (upload)
- QUIT
- Fork-per-session concurrency
- Filesystem confinement under a configured export root
- `compile_commands.json` for IDE tooling

## Build

Requires GCC ≥ 12 or Clang ≥ 15 on Linux.

```
make
```

The binary is placed at `build/ftp-server`.

## Usage

```
build/ftp-server -r ROOT -u USER -p PASS [-b ADDR] [-P PORT] [-m PASV_MIN] [-M PASV_MAX]
```

| Flag | Default | Description |
|------|---------|-------------|
| `-r ROOT` | required | Export root directory |
| `-u USER` | required | Login username |
| `-p PASS` | required | Login password |
| `-b ADDR` | `0.0.0.0` | Bind address |
| `-P PORT` | `2121` | Control port |
| `-m PASV_MIN` | `50000` | Passive port range start |
| `-M PASV_MAX` | `50100` | Passive port range end |

### Example

```
mkdir -p /tmp/ftp-root
build/ftp-server -r /tmp/ftp-root -u alice -p hunter2 -P 2121
```

## Testing

The server has its own integration test suite in `tests/`. Each test
forks and execs the real server binary and exercises it end-to-end
using the [ftp-client](https://github.com/ESikich/ftp_client) library.

```
make test
```

This automatically builds the client library if needed, then runs:

| Test | What it covers |
|------|----------------|
| `test_server_greeting` | 220 on connect |
| `test_server_auth` | login, PWD, QUIT |
| `test_server_auth_fail` | wrong password → 530 |
| `test_server_list` | LIST and NLST |
| `test_server_retr` | download a file; missing file → 550 |
| `test_server_stor` | upload a file and verify contents |
| `test_server_dele` | delete a file; missing file → 550 |
| `test_server_traversal` | `../../etc` escape rejected; cwd unchanged |

To test manually with the client shell:

```
../ftp_client/build/ftp-client
ftp> open 127.0.0.1 2121
ftp> user alice hunter2
ftp> list
ftp> retr somefile.txt
ftp> quit
```

## Design

See [ftp-server-design.md](ftp-server-design.md) for the full design document.
