#!/usr/bin/env python3
"""Compare host and target main.npdm files without running either executable."""
import argparse
import json
from npdm_policy import compare_policies, read_policy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="Host title's main.npdm (matching installed version)")
    parser.add_argument("target", help="Backup title's main.npdm")
    args = parser.parse_args()
    try:
        result = compare_policies(read_policy(args.host), read_policy(args.target))
    except (OSError, ValueError) as exc:
        print(json.dumps({"launchability": "UNDETERMINED", "error": str(exc)}, indent=2))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
