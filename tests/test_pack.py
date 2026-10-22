import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helpers
sys.path.insert(0, helpers.sys_path)
import bootimg  # noqa: E402
import manifest  # noqa: E402

MAKE_BOOT = os.path.join(helpers.ROOT, "packer", "make_boot.py")
PACKER = os.path.join(helpers.ROOT, "build", "packer", "MultiBootKernelPatcher")


class PackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.exists(PACKER):
            subprocess.run(["make", "packer"], cwd=helpers.ROOT, check=True,
                           capture_output=True)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = self.tmp.name
        self.stock_kernel = helpers.make_image()
        self.stock = os.path.join(self.d, "boot_stock.img")
        open(self.stock, "wb").write(helpers.make_v4_boot(self.stock_kernel))
        self.linux = os.path.join(self.d, "Image")
        open(self.linux, "wb").write(helpers.make_image(16384))
        self.dtb = helpers.make_dtb(os.path.join(self.d, "sheng.dtb"))
        self.runtime = os.path.join(self.d, "runtime.bin")
        open(self.runtime, "wb").write(b"\0" * (4 * 1024 * 1024))
        self.out = os.path.join(self.d, "boot_a.img")

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, *extra):
        cmd = [sys.executable, MAKE_BOOT,
               "--stock-boot", self.stock,
               "--linux-image", self.linux,
               "--dtb", self.dtb,
               "--runtime", self.runtime,
               "--out", self.out,
               "--work", os.path.join(self.d, "work"), *extra]
        return subprocess.run(cmd, capture_output=True, text=True)

    def load(self):
        image = bootimg.parse(open(self.out, "rb").read())
        m = manifest.parse(image["kernel"])
        return image, m

    def test_build_layout(self):
        p = self.build()
        self.assertEqual(p.returncode, 0, p.stderr)
        image, m = self.load()
        self.assertEqual(image["version"], 4)
        self.assertEqual(image["kernel_size"], len(image["kernel"]))
        self.assertEqual(m["default_entry"], 0)
        self.assertEqual(m["timeout_ms"], 8000)
        self.assertEqual([e["name"] for e in m["entries"]],
                         ["Android", "Linux", "sheng-dtb", "Attached Shim"])
        self.assertEqual(m["entries"][0]["type_name"], "Linux")
        self.assertIn("BASE_IMAGE", m["entries"][0]["flag_names"])
        self.assertEqual(m["entries"][0]["size"], len(self.stock_kernel))
        self.assertIn("HAS_DTB", m["entries"][1]["flag_names"])
        self.assertEqual(m["entries"][1]["dtb_entry"], 2)
        self.assertGreaterEqual(m["entries"][2]["size"], os.path.getsize(self.dtb))
        self.assertEqual(m["entries"][3]["size"], 4 * 1024 * 1024)

    def test_linux_default_and_bootargs(self):
        p = self.build("--default", "linux", "--timeout-ms", "3000",
                       "--bootargs", "root=PARTLABEL=linux rw console=tty0")
        self.assertEqual(p.returncode, 0, p.stderr)
        image, m = self.load()
        self.assertEqual(m["default_entry"], 1)
        self.assertEqual(m["timeout_ms"], 3000)
        entry = m["entries"][2]
        dtb = image["kernel"][entry["offset"]:entry["offset"] + entry["size"]]
        dtb_path = os.path.join(self.d, "out.dtb")
        open(dtb_path, "wb").write(dtb)
        got = subprocess.run(["fdtget", "-t", "s", dtb_path, "/chosen", "bootargs"],
                             capture_output=True, text=True, check=True).stdout.strip()
        self.assertEqual(got, "root=PARTLABEL=linux rw console=tty0")

    def test_initrd_entry(self):
        initrd = os.path.join(self.d, "initrd.img")
        open(initrd, "wb").write(b"x" * 4096)
        p = self.build("--initrd", initrd)
        self.assertEqual(p.returncode, 0, p.stderr)
        image, m = self.load()
        self.assertEqual([e["name"] for e in m["entries"]],
                         ["Android", "Linux", "sheng-dtb", "initrd", "Attached Shim"])
        self.assertEqual(m["entries"][3]["type_name"], "Blob")
        entry = m["entries"][2]
        dtb = image["kernel"][entry["offset"]:entry["offset"] + entry["size"]]
        dtb_path = os.path.join(self.d, "out.dtb")
        open(dtb_path, "wb").write(dtb)
        idx = subprocess.run(["fdtget", "-t", "u", dtb_path, "/chosen", "kshim,initrd-entry"],
                             capture_output=True, text=True, check=True).stdout.strip()
        self.assertEqual(idx, "3")

    def test_rejects_oversized_payload(self):
        p = self.build("--partition-size", "1048576")
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("exceeds partition budget", p.stderr)

    def test_accepts_gzip_linux_image(self):
        raw = helpers.make_image()
        open(self.linux, "wb").write(helpers.gzip_bytes(raw))
        p = self.build()
        self.assertEqual(p.returncode, 0, p.stderr)
        _, m = self.load()
        linux = [e for e in m["entries"] if e["name"] == "Linux"][0]
        self.assertEqual(linux["size"], len(raw))


if __name__ == "__main__":
    unittest.main()
