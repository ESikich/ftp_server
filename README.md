# ftp-server

Minimal, correct FTP server in C23 (RFC 959).

## Features

- USER / PASS authentication (static configured credentials)
- SYST, NOOP, TYPE A / TYPE I
- PWD, CWD, CDUP, MKD
- PASV (passive mode, IPv4 only)
- LIST, NLST
- RETR (download), STOR (upload)
- QUIT
- Fork-per-session concurrency
- Filesystem confinement under a configured export root

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

The server is tested with the companion [ftp-client](https://github.com/ESikich/ftp_client). Client integration tests pass against both the client's own mock servers and the real server.

To run the client test suite:

```
cd ../ftp_client
make CFLAGS="-std=c2x -Wall -Wextra -Wpedantic -Werror -O2" test
```

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
