#!/usr/bin/env python3
"""Shared stdlib-only driver for QEMU serial-menu fixture selection."""
import os
import re
import select
import subprocess
import time


def run_with_label(command, label, timeout):
    """Run command, selecting the numbered row whose exact label is rendered.

    stdout/stderr are merged. The row number comes from the displayed menu, so
    fixture reordering is handled by label. On deadline expiry the process is
    killed and TimeoutExpired carries every captured byte.
    """
    command = [str(c) for c in command]
    print("+", " ".join(command), flush=True)
    line_re = re.compile(r"^\s*(\d+)\.\s+" + re.escape(label) +
                         r"(?:\s+\[[^\]\n]*\])?\s*$")
    deadline = time.monotonic() + timeout
    output = bytearray()
    pending = b""
    selected = False
    timed_out = False
    process = subprocess.Popen(command, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    try:
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                timed_out = True
                break
            ready, _, _ = select.select([process.stdout], [], [], remaining)
            if not ready:
                continue
            chunk = os.read(process.stdout.fileno(), 65536)
            if not chunk:
                break
            output += chunk
            if not selected:
                lines = (pending + chunk).split(b"\n")
                pending = lines.pop()
                for line in lines:
                    match = line_re.match(line.decode("utf-8", "replace"))
                    if match:
                        process.stdin.write(match.group(1).encode() + b"\n")
                        process.stdin.flush()
                        process.stdin.close()
                        process.stdin = None  # communicate() must not flush it
                        selected = True
                        break
        if not timed_out:
            remaining = max(0.0, deadline - time.monotonic())
            try:
                process.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                timed_out = True
    finally:
        if process.poll() is None:
            process.kill()
        extra, _ = process.communicate()
        output += extra
    if timed_out:
        raise subprocess.TimeoutExpired(command, timeout, output=bytes(output))
    return subprocess.CompletedProcess(command, process.returncode, bytes(output))
