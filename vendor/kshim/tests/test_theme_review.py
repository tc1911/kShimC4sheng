"""Host-only regression tests for the authentic CI frame viewer."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / 'tools/build_theme_review.py'
THEMES = ('FLAT', 'MATERIAL3')
SIZES = ((2, 2),)
STAGES = ('entry-120ms', 'ready-400ms', 'focus-130ms', 'confirm-90ms')


def png(color, level=6):
    def chunk(kind, value):
        return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind + value) & 0xffffffff)
    raw = (b'\0' + bytes(color) * 2) * 2
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, level)) + chunk(b'IEND', b'')


class ThemeReviewTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not MODULE.exists():
            cls.m = None
            return
        spec = importlib.util.spec_from_file_location('theme_review', MODULE)
        cls.m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.m)

    def test_builder_exists(self):
        self.assertTrue(MODULE.is_file(), 'CI artifact viewer has not been implemented')

    def setUp(self):
        if self.m is None and self._testMethodName != 'test_builder_exists':
            self.skipTest('Waiting for the CI artifact viewer')
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def fixture(self, name='new', commit='b' * 40, color=(40, 80, 160), level=6):
        root = self.root / name
        for theme in THEMES:
            p = root / ('menu-' + theme) / 'previews'
            p.mkdir(parents=True)
            frames = []
            for stage in STAGES:
                data = png((0, 0, 0) if theme == 'FLAT' else color, level)
                file = f'menu-2x2-{stage}.png'
                (p / file).write_bytes(data)
                frames.append(dict(file=file, width=2, height=2, sha256=hashlib.sha256(data).hexdigest()))
            (p / 'manifest.json').write_text(json.dumps(frames))
            (p / 'source-commit.txt').write_text(commit + '\n')
            log = root / ('menu-' + theme) / 'build-menu/Testing/Temporary/LastTest.log'
            log.parent.mkdir(parents=True)
            log.write_text('1/1 Testing: example\nTest Passed.\nEnd testing: now\n')
        return root

    def read(self, root, **kwargs):
        return self.m.read_set(root, themes=THEMES, sizes=SIZES, **kwargs)

    def manifest(self, root):
        p = root / 'menu-MATERIAL3/previews/manifest.json'
        return p, json.loads(p.read_text())

    def test_read_complete_set(self):
        d = self.read(self.fixture())
        self.assertEqual(d['source'], 'b' * 40)
        self.assertEqual(len(d['frames']), 8)
        self.assertEqual(d['tests_passed'], 2)

    def test_reject_missing_frame(self):
        r = self.fixture(); p, a = self.manifest(r)
        p.write_text(json.dumps(a[:-1]))
        with self.assertRaisesRegex(ValueError, 'Missing'): self.read(r)

    def test_reject_duplicate_key(self):
        r = self.fixture(); p, a = self.manifest(r)
        p.write_text(json.dumps(a + a[:1]))
        with self.assertRaisesRegex(ValueError, 'Duplicate'): self.read(r)

    def test_reject_traversal(self):
        r = self.fixture(); p, a = self.manifest(r)
        a[0]['file'] = '../outside.png'; p.write_text(json.dumps(a))
        with self.assertRaises(ValueError): self.read(r)

    def test_reject_symlink_escape(self):
        r = self.fixture(); p, a = self.manifest(r)
        file = p.parent / a[0]['file']; data = file.read_bytes(); file.unlink()
        outside = self.root / 'outside.png'; outside.write_bytes(data); file.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'escapes'): self.read(r)

    def test_reject_bad_hash(self):
        r = self.fixture(); p, a = self.manifest(r)
        a[0]['sha256'] = '0' * 64; p.write_text(json.dumps(a))
        with self.assertRaisesRegex(ValueError, 'hash'): self.read(r)

    def test_reject_bad_dimensions(self):
        r = self.fixture(); p, a = self.manifest(r)
        a[0]['width'] = 3; p.write_text(json.dumps(a))
        with self.assertRaisesRegex(ValueError, 'dimensions'): self.read(r)

    def test_reject_bad_crc_with_updated_hash(self):
        r = self.fixture(); p, a = self.manifest(r)
        file = p.parent / a[0]['file']; data = bytearray(file.read_bytes()); data[-1] ^= 1
        file.write_bytes(data); a[0]['sha256'] = hashlib.sha256(data).hexdigest(); p.write_text(json.dumps(a))
        with self.assertRaisesRegex(ValueError, 'CRC'): self.read(r)

    def test_reject_mixed_source_stamps(self):
        r = self.fixture()
        (r / 'menu-FLAT/previews/source-commit.txt').write_text('c' * 40)
        with self.assertRaisesRegex(ValueError, 'source'): self.read(r)

    def test_missing_source_needs_explicit_identity(self):
        r = self.fixture()
        for p in r.glob('*/previews/source-commit.txt'): p.unlink()
        with self.assertRaisesRegex(ValueError, 'source'): self.read(r)
        self.assertEqual(self.read(r, supplied_source='a' * 40)['source_kind'], 'supplied')

    def test_reject_overriding_source_stamp(self):
        with self.assertRaisesRegex(ValueError, 'source'): self.read(self.fixture(), supplied_source='f' * 40)

    def test_reject_failed_test_log(self):
        r = self.fixture(); log = next(r.glob('*/build-menu/Testing/Temporary/LastTest.log'))
        log.write_text('Test Passed.\nTest Failed.\nEnd testing: now\n')
        with self.assertRaisesRegex(ValueError, 'test log'): self.read(r)

    def test_reject_incomplete_test_log(self):
        r = self.fixture(); log = next(r.glob('*/build-menu/Testing/Temporary/LastTest.log'))
        log.write_text('Test Passed.\n')
        with self.assertRaisesRegex(ValueError, 'test log'): self.read(r)

    def test_pair_accepts_decoded_flat_pixels_not_encoding(self):
        old = self.read(self.fixture('old', 'a' * 40, (70, 70, 70), level=0))
        new = self.read(self.fixture())
        self.assertEqual(self.m.compare_sets(old, new)['flat_matches'], 4)

    def test_pair_rejects_flat_regression(self):
        old = self.read(self.fixture('old', 'a' * 40, (70, 70, 70)))
        new = self.read(self.fixture())
        next(f for f in new['frames'] if f['theme'] == 'FLAT')['pixel_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'FLAT'): self.m.compare_sets(old, new)

    def test_pair_rejects_relabelled_old_frames(self):
        old = self.read(self.fixture('old', 'a' * 40))
        new = self.read(self.fixture())
        with self.assertRaisesRegex(ValueError, 'unchanged'): self.m.compare_sets(old, new)

    def test_pair_rejects_same_commit_identity(self):
        old = self.read(self.fixture('old', 'b' * 40, (70, 70, 70)))
        new = self.read(self.fixture())
        with self.assertRaisesRegex(ValueError, 'source'): self.m.compare_sets(old, new)

    def test_viewer_keeps_bytes_and_identity(self):
        old = self.read(self.fixture('old', 'a' * 40, (70, 70, 70)))
        new = self.read(self.fixture())
        out = self.root / 'review'
        result = self.m.write_review(new, out, old)
        self.assertEqual(result['new_frames'], 8)
        self.assertEqual(result['old_frames'], 8)
        self.assertTrue((out / 'index.html').is_file())
        self.assertIn('data:image/png;base64,', (out / 'standalone.html').read_text())
        for f in new['frames']:
            self.assertEqual((out / 'new' / f['path']).read_bytes(), new['bytes'][f['key']])
        with self.assertRaisesRegex(ValueError, 'exists'): self.m.write_review(new, out, old)

    def test_current_only_does_not_claim_flat_comparison(self):
        out = self.root / 'review'
        result = self.m.write_review(self.read(self.fixture()), out)
        self.assertIsNone(result['flat_matches'])
        self.assertEqual(result['old_frames'], 0)


if __name__ == '__main__':
    unittest.main()
