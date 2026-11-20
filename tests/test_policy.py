import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from npdm_policy import parse_policy, compare_policies, service_covered


def policy():
    data = bytearray(0x480)
    data[:4] = b"META"
    struct.pack_into("<IIII", data, 0x70, 0x400, 0x80, 0x80, 0x340)
    data[0x380:0x384] = b"ACID"
    data[0x400:0x404] = b"ACI0"
    struct.pack_into("<Q", data, 0x410, 0x0400000000001000)
    struct.pack_into("<II", data, 0x428, 0x40, 5)
    data[0x440:0x445] = b"\x03sm:m"
    struct.pack_into("<II", data, 0x430, 0x48, 4)
    struct.pack_into("<I", data, 0x448, (1 << 5) | 15)
    return data


class PolicyTests(unittest.TestCase):
    def test_declared_services_and_syscalls(self):
        result = parse_policy(policy())
        self.assertEqual(result["service_access"], ["sm:m"])
        self.assertEqual(result["declared_syscalls"], [0])
        self.assertEqual(result["platform"], "Switch2")
        self.assertFalse(result["signature_verified"])

    def test_truncated_service_rejected(self):
        data = policy(); data[0x440] = 15
        with self.assertRaises(ValueError): parse_policy(data)

    def test_section_outside_file_rejected(self):
        data = policy(); struct.pack_into("<I", data, 0x70, 0xfffffff0)
        with self.assertRaises(ValueError): parse_policy(data)

    def test_overlap_rejected(self):
        data = policy(); struct.pack_into("<I", data, 0x74, 0xffff)
        with self.assertRaises(ValueError): parse_policy(data)

    def test_four_byte_meta_rejected(self):
        with self.assertRaises(ValueError): parse_policy(b"META")

    def test_equal_policies_never_claim_launchability(self):
        p = parse_policy(policy())
        result = compare_policies(p, p)
        self.assertEqual(result["launchability"], "UNDETERMINED")
        self.assertEqual(result["target_services_not_declared_by_host"], [])

    def test_missing_service_detected(self):
        p = parse_policy(policy()); other = dict(p, service_access=["audout:u"])
        self.assertEqual(compare_policies(p, other)["target_services_not_declared_by_host"], ["audout:u"])

    def test_prefix_wildcard_only(self):
        self.assertTrue(service_covered("fsp-srv", ["fsp-*"]))
        self.assertFalse(service_covered("fsp-srv", ["fsp-?"]))


if __name__ == "__main__": unittest.main()
