import importlib.util
import struct
import tempfile
import unittest
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from test_policy import policy as npdm_fixture
spec = importlib.util.spec_from_file_location("inspector", ROOT / "scripts/inspect-backup.py")
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


def nso():
    data = bytearray(0x130)
    data[:4] = b"NSO0"
    for i in range(3):
        struct.pack_into("<III", data, 0x10 + 16*i, 0x100 + 16*i, 0x1000*i, 16)
    return data


class BackupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "exefs").mkdir()
        (self.root / "romfs").mkdir()
        (self.root / "exefs/main.npdm").write_bytes(npdm_fixture())
        self.main = self.root / "exefs/main"
        self.main.write_bytes(nso())

    def test_consistent_headers_never_claim_launchability(self):
        before = self.main.read_bytes()
        result = inspector.inspect_backup(self.root)
        self.assertEqual(result["preflight"], "BASIC_CHECKS_PASSED")
        self.assertEqual(result["launchability"], "UNDETERMINED")
        self.assertEqual(before, self.main.read_bytes())
        self.assertIn("signatures", " ".join(result["limitations"]))

    def test_four_byte_nso_is_rejected(self):
        self.main.write_bytes(b"NSO0")
        self.assertTrue(inspector.inspect_backup(self.root)["errors"])

    def test_segment_outside_file_is_rejected(self):
        data = nso()
        struct.pack_into("<I", data, 0x10, 0xfffffff0)
        self.main.write_bytes(data)
        self.assertTrue(inspector.inspect_backup(self.root)["errors"])

    def test_overlapping_segments_are_rejected(self):
        data = nso()
        struct.pack_into("<I", data, 0x20, 0x100)
        self.main.write_bytes(data)
        self.assertTrue(inspector.inspect_backup(self.root)["errors"])

    def test_compressed_range_is_checked(self):
        data = nso()
        struct.pack_into("<I", data, 0xC, 1)
        struct.pack_into("<I", data, 0x60, 0xffff)
        self.main.write_bytes(data)
        self.assertTrue(inspector.inspect_backup(self.root)["errors"])

    def test_optional_module_is_inspected(self):
        (self.root / "exefs/subsdk0").write_bytes(b"NSO0")
        result = inspector.inspect_backup(self.root)
        self.assertTrue(any("subsdk0" in e for e in result["errors"]))

    def test_missing_romfs_is_reported(self):
        (self.root / "romfs").rmdir()
        self.assertIn("Missing romfs directory", inspector.inspect_backup(self.root)["errors"])

    def test_missing_folder_returns_report(self):
        result = inspector.inspect_backup(self.root / "absent")
        self.assertEqual(result["preflight"], "ISSUES_FOUND")

    def test_base_dump_layout_is_detected(self):
        parent = self.root / "game"
        parent.mkdir()
        base = parent / "base"
        base.mkdir()
        (self.root / "exefs").rename(base / "exefs")
        (self.root / "romfs").rename(base / "romfs")
        result = inspector.inspect_backup(parent)
        self.assertFalse(result["errors"])
        self.assertEqual(result["folder"], str(base))

    def test_listing_size_mismatch_is_reported(self):
        (self.root / "exefs/.listing").write_text("-r--r--r-- 1 ftp ftp 999 Jan 01 1970 main\n")
        result = inspector.inspect_backup(self.root, inventory=True)
        self.assertTrue(any("size mismatch" in e for e in result["errors"]))

    def test_listing_parent_path_is_rejected(self):
        (self.root / "exefs/.listing").write_text("-r--r--r-- 1 ftp ftp 999 Jan 01 1970 ../outside\n")
        result = inspector.inspect_backup(self.root, inventory=True)
        self.assertTrue(any("Unsafe listing name" in e for e in result["errors"]))


if __name__ == "__main__":
    unittest.main()
