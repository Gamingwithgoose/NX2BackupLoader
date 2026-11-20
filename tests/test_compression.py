import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("estimate", ROOT / "scripts/estimate-nup-size.py")
estimate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(estimate)
try:
    from compression import zstd
except ImportError:
    zstd = None


@unittest.skipUnless(zstd, "Python 3.14+ compression.zstd required")
class CompressionTests(unittest.TestCase):
    def test_compression_dedup_and_read_only(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            content = b"owned-test-fixture\n" * 10000
            (root / "a.bin").write_bytes(content)
            (root / "b.bin").write_bytes(content)
            (root / ".listing").write_text("excluded")
            result = estimate.measure(root)
            self.assertEqual(result["files"], 2)
            self.assertEqual(result["deduplicated_files"], 1)
            self.assertLess(result["unique_payload_bytes"], len(content))
            self.assertFalse(result["archive_created"])
            self.assertEqual((root / "a.bin").read_bytes(), content)
            self.assertEqual(len(list(root.iterdir())), 3)

    def test_missing_folder_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(ValueError): estimate.measure(Path(temp) / "missing")


if __name__ == "__main__": unittest.main()
