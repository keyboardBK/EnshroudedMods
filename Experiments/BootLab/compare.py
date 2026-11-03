"""Read-only comparison; candidate similarity is never evidence of permanent identity."""
import argparse
import csv
import json
from collections import Counter
from pathlib import Path


def compare(before, after):
    def load(path):
        with Path(path).open(encoding="utf-8-sig", newline="") as source:
            rows = list(csv.DictReader(source))
        for row in rows:
            for name in ("session", "inventory", "slot", "pide", "item"):
                int(row[name])
            if not row.get("fingerprint"):
                raise ValueError("Missing observation fingerprint")
        return rows
    left, right = load(before), load(after)
    lc = Counter(r["fingerprint"] for r in left)
    rc = Counter(r["fingerprint"] for r in right)
    return {
        "before": len(left), "after": len(right),
        "duplicate_fingerprints_before": sum(n - 1 for n in lc.values() if n > 1),
        "duplicate_fingerprints_after": sum(n - 1 for n in rc.values() if n > 1),
        "shared_fingerprint_groups": len(lc.keys() & rc.keys()),
        "persistent_identity_verified": False,
        "reason": "PIDE/session and visible item properties are not a verified permanent identity; identical shoes can collide and mutable properties change."
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("before")
    parser.add_argument("after")
    args = parser.parse_args()
    print(json.dumps(compare(args.before, args.after), ensure_ascii=False, indent=2))
