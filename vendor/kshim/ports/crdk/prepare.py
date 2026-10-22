#!/usr/bin/env python3
"""Materialize the pinned CrDK tree plus verified patches, without editing it."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile


def prepare(source, output):
    port = Path(__file__).resolve().parent
    manifest = json.loads((port / "UPSTREAM.json").read_text())
    revision = manifest["revision"]
    actual = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != revision:
        raise ValueError(f"CrDK must be at {revision}, found {actual}")
    patches = []
    for entry in manifest["patches"]:
        patch = port / "patches" / entry["file"]
        if hashlib.sha256(patch.read_bytes()).hexdigest() != entry["sha256"]:
            raise ValueError(f"CrDK patch hash mismatch: {patch.name}")
        patches.append(patch)
    signature = hashlib.sha256((port / "UPSTREAM.json").read_bytes() +
                               Path(__file__).read_bytes()).hexdigest()
    stamp = output / ".kshim-crdk-stamp"
    if stamp.exists() and stamp.read_text() == signature:
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="crdk-", dir=output.parent) as temporary:
        tree = Path(temporary) / "tree"
        tree.mkdir()
        archive = subprocess.check_output(
            ["git", "-C", str(source), "archive", "--format=tar", revision])
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            tar.extractall(tree, filter="data")
        for patch in patches:
            # Do not let git discover the enclosing kShim checkout and silently
            # filter patch paths relative to that repository's subdirectory.
            env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(tree.parent))
            subprocess.run(["git", "apply", "--check", str(patch)], cwd=tree, env=env, check=True)
            subprocess.run(["git", "apply", str(patch)], cwd=tree, env=env, check=True)
        (tree / ".kshim-crdk-stamp").write_text(signature)
        if output.exists():
            shutil.rmtree(output)
        tree.rename(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.source.resolve(), args.output.resolve())
