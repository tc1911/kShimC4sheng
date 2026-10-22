#!/usr/bin/env python3
"""Exercise packaging checks against real build_andromeda_dt artifacts.

Run after tools/build_andromeda_dt.py; optionally set KSHIM_ANDROMEDA_DT_DIR.
All negative cases mutate private copies, never the kernel build output.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_andromeda_dt import BASES, parse_bundle
from build_hdk import validate_andromeda_bundle


class AndromedaBundleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.artifacts = Path(os.environ.get("KSHIM_ANDROMEDA_DT_DIR",
                                          ROOT / "build/andromeda-dt")).resolve()
        if not (cls.artifacts / "manifest.json").is_file():
            raise RuntimeError("Build the real Andromeda DT artifacts before running this test")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="kshim-dtb-test-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.bundle = self.directory / "android-andromeda-dtbs.bin"
        self.manifest_path = self.directory / "manifest.json"
        shutil.copyfile(self.artifacts / self.bundle.name, self.bundle)
        shutil.copyfile(self.artifacts / "manifest.json", self.manifest_path)
        self.manifest = json.loads(self.manifest_path.read_text())
        for name in ("obj", "merged"):
            (self.directory / name).symlink_to(self.artifacts / name, target_is_directory=True)

    def save_manifest(self):
        self.manifest_path.write_text(json.dumps(self.manifest))

    def test_real_bundle_replays_all_combinations(self):
        data, provenance, snapshot = validate_andromeda_bundle(self.bundle)
        self.assertEqual(data, self.bundle.read_bytes())
        self.assertEqual(provenance["validated_combinations"], 32)
        self.assertEqual(len(provenance["overlay_sha256"]), 8)
        self.assertEqual(provenance["bases"]["sm8150p-v2"]["msm_id"], [361, 0x20000])
        self.assertEqual(provenance["manifest_sha256"], hashlib.sha256(snapshot).hexdigest())

    def test_manifest_is_required(self):
        self.manifest_path.unlink()
        with self.assertRaises(FileNotFoundError):
            validate_andromeda_bundle(self.bundle)

    def test_other_kernel_revision_is_rejected(self):
        self.manifest["revision"] = "0" * 40
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "pinned official"):
            validate_andromeda_bundle(self.bundle)

    def test_bundle_hash_is_checked(self):
        data = bytearray(self.bundle.read_bytes())
        data[-1] ^= 1
        self.bundle.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "bundle SHA256"):
            validate_andromeda_bundle(self.bundle)

    def test_each_base_hash_is_checked(self):
        self.manifest["bases"]["sm8150p-v2"] = "0" * 64
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "member SHA256"):
            validate_andromeda_bundle(self.bundle)

    def test_32_duplicates_do_not_count_as_coverage(self):
        records = self.manifest["validated_combinations"]
        records[-1] = records[0]
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "duplicated"):
            validate_andromeda_bundle(self.bundle)

    def test_overlay_board_identity_is_checked(self):
        self.manifest["validated_combinations"][0]["board_id"] = ["8", "0"]
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "overlay validation"):
            validate_andromeda_bundle(self.bundle)

    def test_fresh_checksum_does_not_hide_wrong_soc(self):
        members = parse_bundle(self.bundle.read_bytes())
        first = self.directory / "wrong-soc.dtb"
        first.write_bytes(members[0])
        subprocess.run(["fdtput", "-t", "x", str(first), "/", "qcom,msm-id",
                        "1", "10000"], check=True)
        members[0] = first.read_bytes()
        self.bundle.write_bytes(b"".join(members))
        self.manifest["bundle_sha256"] = hashlib.sha256(self.bundle.read_bytes()).hexdigest()
        self.manifest["bases"][BASES[0]] = hashlib.sha256(members[0]).hexdigest()
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "SoC/version/board"):
            validate_andromeda_bundle(self.bundle)

    def test_false_overlay_validation_is_rejected_by_replay(self):
        self.manifest["validated_combinations"][0]["sha256"] = "0" * 64
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "replay SHA256"):
            validate_andromeda_bundle(self.bundle)


if __name__ == "__main__":
    unittest.main()
