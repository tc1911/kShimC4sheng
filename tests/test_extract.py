import json
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import helpers


class ExtractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def run_extract(self, *args):
        cmd = [sys.executable, os.path.join(helpers.ROOT, "packer", "extract.py"), *args]
        return subprocess.run(cmd, capture_output=True, text=True)

    def test_v4_stock_kernel(self):
        kernel = helpers.make_image()
        boot = os.path.join(self.d, "stock.img")
        open(boot, "wb").write(helpers.make_v4_boot(kernel))
        out = os.path.join(self.d, "out")
        p = self.run_extract("--stock", "--boot", boot, "--out", out, "--prefix", "stock")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(open(os.path.join(out, "stock-kernel.img"), "rb").read(), kernel)
        stock_boot = open(os.path.join(out, "stock-boot.img"), "rb").read()
        self.assertEqual(stock_boot[:8], b"ANDROID!")
        self.assertEqual(len(stock_boot), 4096 + len(kernel))

    def test_v0_gzip_plus_dtb_and_initrd(self):
        kernel = helpers.make_image()
        dtb = helpers.make_dtb(os.path.join(self.d, "sheng.dtb"))
        dtb_data = open(dtb, "rb").read()
        ramdisk = b"initrd-data" * 100
        boot = os.path.join(self.d, "linux.img")
        open(boot, "wb").write(helpers.make_v0_boot(
            helpers.gzip_bytes(kernel) + dtb_data, ramdisk))
        out = os.path.join(self.d, "out")
        p = self.run_extract("--boot", boot, "--out", out, "--prefix", "linux")
        self.assertEqual(p.returncode, 0, p.stderr)
        info = json.loads(p.stdout)
        self.assertEqual(open(os.path.join(out, "linux-Image"), "rb").read(), kernel)
        self.assertEqual(open(os.path.join(out, "linux.dtb"), "rb").read(), dtb_data)
        self.assertEqual(open(os.path.join(out, "linux-initrd.img"), "rb").read(), ramdisk)
        self.assertEqual(info["dtb"]["size"], len(dtb_data))

    def test_rejects_gzip_as_stock(self):
        boot = os.path.join(self.d, "bad.img")
        open(boot, "wb").write(helpers.make_v4_boot(helpers.gzip_bytes(helpers.make_image())))
        p = self.run_extract("--stock", "--boot", boot, "--out", self.d, "--prefix", "bad")
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("gzip", p.stderr)


if __name__ == "__main__":
    unittest.main()
