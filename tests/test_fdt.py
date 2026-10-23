import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "packer"))
import fdt  # noqa: E402


class FdtEditTest(unittest.TestCase):
    def make_tree(self):
        root = fdt.Node("")
        chosen = fdt.Node("chosen", root)
        chosen.props.append(["bootargs", b"old\0"])
        root.children.append(chosen)
        return fdt.Fdt(root, b"\0" * 16)

    def test_roundtrip_and_edit(self):
        tree = self.make_tree()
        tree.set("/chosen", "bootargs", b"root=/dev/sda30\0")
        tree.set("/chosen", "kshim,initrd-entry", (3).to_bytes(4, "big"))
        blob = tree.tobytes()
        again = fdt.Fdt.parse(blob)
        self.assertEqual(again.get("/chosen", "bootargs"), b"root=/dev/sda30\0")
        self.assertEqual(again.get("/chosen", "kshim,initrd-entry"), b"\0\0\0\3")

    def test_dtc_accepts_output(self):
        dtc = shutil.which("dtc")
        if not dtc:
            self.skipTest("dtc not installed")
        tree = self.make_tree()
        tree.set("/chosen", "bootargs", b"root=PARTLABEL=linux rw\0")
        with tempfile.NamedTemporaryFile(suffix=".dtb", delete=False) as f:
            path = f.name
        try:
            tree.write(path)
            proc = subprocess.run([dtc, "-I", "dtb", "-O", "dts", path],
                                  capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, proc.stderr)
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
