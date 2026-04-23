# ftp-server

Minimal, correct FTP server in C23 (RFC 959).

## Features

- USER / PASS authentication with configured accounts
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

To regenerate IDE tooling without building the server, run:

```
make compile_commands.json
```

This rewrites the checked-in compilation database from the current `Makefile`
settings, so editors like `clangd` stay aligned with the build flags.

## Configuration

The server is configured with TOML. Each user gets a home directory relative
to the export root, a password hash, and an explicit permission list.

Example:

```toml
root = "/srv/ftp"
bind = "0.0.0.0"
port = 2121
pasv_min = 50000
pasv_max = 50100

[[users]]
name = "alice"
hash = "$6$ftpserver$W.gEv68KkenSkpTDtZ/mGL.nun.GJuqzZsXFx5/.XiOhG/gdcWXTAQgexO8jDkHC96G6cN58tliCMrXZtq3iw."
home = "alice"
perms = ["read", "write", "delete", "mkdir"]

[[users]]
name = "bob"
hash = "$6$ftpserver$S/cjclfGUKh0lhON1MWrQGMbc6bfIaJYICwv6ObI6E.riONdPawFRLolC5RvV7ZcC89dwQRys0WiGqiRBdYGV1"
home = "shared/reports"
perms = ["read"]
```

Permission names are:

- `read` for `LIST`, `NLST`, `RETR`, and `CWD`
- `write` for `STOR`
- `delete` for `DELE`
- `mkdir` for `MKD`

Permission matrix:

| FTP command | Required permission |
|-------------|---------------------|
| `LIST` | `read` |
| `NLST` | `read` |
| `RETR` | `read` |
| `CWD` / `CDUP` | `read` |
| `STOR` | `write` |
| `DELE` | `delete` |
| `MKD` | `mkdir` |

Start the server with:

```bash
build/ftp-server -c server.conf
```

The `home` path is resolved under `root`, so `bob` can only see files beneath
`/srv/ftp/shared/reports`.

To generate a password hash for a new user, use:

```bash
openssl passwd -6 -salt ftpserver 'your-password'
```

The output is the full hash string to paste after `hash =` in the user table.

## Usage

```
build/ftp-server -c CONFIG [-b ADDR] [-P PORT] [-m PASV_MIN] [-M PASV_MAX]
```

| Flag | Default | Description |
|------|---------|-------------|
| `-c CONFIG` | required | Server configuration file |
| `-b ADDR` | `0.0.0.0` | Bind address |
| `-P PORT` | `2121` | Control port |
| `-m PASV_MIN` | `50000` | Passive port range start |
| `-M PASV_MAX` | `50100` | Passive port range end |

### Example

```
mkdir -p /tmp/ftp-root
cat > /tmp/ftp-server.conf <<'EOF'
root = "/tmp/ftp-root"

[[users]]
name = "alice"
hash = "$6$ftpserver$W.gEv68KkenSkpTDtZ/mGL.nun.GJuqzZsXFx5/.XiOhG/gdcWXTAQgexO8jDkHC96G6cN58tliCMrXZtq3iw."
home = "."
perms = ["read", "write", "delete", "mkdir"]
EOF
build/ftp-server -c /tmp/ftp-server.conf -P 2121
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
