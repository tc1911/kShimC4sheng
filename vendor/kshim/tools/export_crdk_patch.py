#!/usr/bin/env python3
"""Export edits on top of the staged reference patch in a private CrDK clone."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
work = Path(sys.argv[1]).resolve()
base = "b6c3335136b7f6b28271917de11e8f11982975fc"
assert subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=work, text=True).strip() == base
subprocess.run(["git", "add", "-N", "."], cwd=work, check=True)
patch = subprocess.check_output(["git", "diff", "--binary"], cwd=work)
port = root / "ports/crdk"
name = "0002-freestanding-resources.patch"
(port / "patches" / name).write_bytes(patch)
manifest = json.loads((port / "UPSTREAM.json").read_text())
manifest["patches"] = [manifest["patches"][0],
    {"file": name, "sha256": hashlib.sha256(patch).hexdigest()}]
(port / "UPSTREAM.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"Exported {len(patch)} bytes: {name}")
