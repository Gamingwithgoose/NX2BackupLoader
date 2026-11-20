import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHELL = shutil.which("powershell.exe") or shutil.which("pwsh")


def candidate():
    data = bytearray(0x3000)
    struct.pack_into("<I", data, 4, 0x100)
    data[0x10:0x14] = b"NRO0"
    struct.pack_into("<I", data, 0x18, len(data))
    for i in range(3):
        struct.pack_into("<II", data, 0x20+8*i, i*0x1000, 0x1000)
    data[0x100:0x104] = b"MOD0"
    for i, magic in enumerate((b"LNY0", b"LNY1", b"LNY2", b"OUN0")):
        offset = 0x11c + 12*i
        data[offset:offset+4] = magic
    struct.pack_into("<II", data, 0x144, 2, 1)
    return data


@unittest.skipUnless(SHELL, "PowerShell required")
class VerifierTests(unittest.TestCase):
    def check(self, data, accepted=False):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "candidate.nro"
            path.write_bytes(data)
            run = subprocess.run([SHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                str(ROOT / "scripts/verify-nro.ps1"), "-Path", str(path)],
                capture_output=True, text=True)
        self.assertEqual(run.returncode == 0, accepted, run.stdout + run.stderr)

    def test_consistent_candidate_is_accepted(self):
        self.check(candidate(), True)

    def test_wrong_platform_is_rejected(self):
        data = candidate(); struct.pack_into("<I", data, 0x144, 1)
        self.check(data)

    def test_unknown_record_version_is_rejected(self):
        data = candidate(); struct.pack_into("<I", data, 0x148, 2)
        self.check(data)

    def test_truncated_image_is_rejected(self):
        self.check(candidate()[:0x1000])

    def test_128_byte_marker_fake_is_rejected(self):
        data = bytearray(128); data[16:20] = b"NRO0"; data[120:124] = b"OUN0"
        self.check(data)

    def test_payload_marker_is_rejected(self):
        data = candidate(); data[0x140:0x144] = b"NONE"; data[0x2000:0x2004] = b"OUN0"
        self.check(data)

    def test_segment_outside_image_is_rejected(self):
        data = candidate(); struct.pack_into("<I", data, 0x30, 0x4000)
        self.check(data)

    def test_overlapping_segments_are_rejected(self):
        data = candidate(); struct.pack_into("<I", data, 0x28, 0)
        self.check(data)

    def test_invalid_module_pointer_is_rejected(self):
        data = candidate(); struct.pack_into("<I", data, 4, 0xfffffffc)
        self.check(data)

    def test_tiny_file_is_rejected(self):
        self.check(b"NRO0")


if __name__ == "__main__":
    unittest.main()
