#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import shlex
import sys


def split_words(value: str) -> list[str]:
    return shlex.split(value) if value else []


def compile_command(cc: str, cpp: str, cflags: str, extra: str, build_dir: str, src: str) -> str:
    parts = [cc]
    parts.extend(split_words(cpp))
    if extra:
        parts.extend(split_words(extra))
    parts.extend(split_words(cflags))
    parts.extend(["-c", "-o", f"{build_dir}/{os.path.splitext(os.path.basename(src))[0]}.o", src])
    return " ".join(parts)


def main() -> int:
    cc = os.environ.get("CC", "cc")
    cpp = os.environ.get("CPP", "")
    cflags = os.environ.get("CFLAGS", "")
    client_inc = os.environ.get("CLIENT_INC", "")
    build_dir = os.environ.get("BUILD_DIR", "build")
    srcs = split_words(os.environ.get("SRCS", ""))
    test_srcs = split_words(os.environ.get("TEST_SRCS", ""))

    entries = []
    for src in srcs:
        entries.append(
            {
                "directory": ".",
                "command": compile_command(cc, cpp, cflags, "", build_dir, src),
                "file": src,
            }
        )
    for src in test_srcs:
        entries.append(
            {
                "directory": ".",
                "command": compile_command(cc, cpp, cflags, client_inc, build_dir, src),
                "file": src,
            }
        )

    json.dump(entries, sys.stdout, indent=4)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
