import csv
from pathlib import Path
import tempfile
import unittest
from compare import compare


class CompareTests(unittest.TestCase):
    def test_identical_duplicates_and_new_session_do_not_prove_identity(self):
        with tempfile.TemporaryDirectory(prefix="bootlab-compare-") as folder:
            left, right = Path(folder) / "before.csv", Path(folder) / "after.csv"
            fields = ["session", "inventory", "slot", "pide", "item", "fingerprint"]
            for path, session, start in [(left, 1, 20), (right, 2, 400)]:
                with path.open("w", newline="", encoding="utf-8") as stream:
                    writer = csv.DictWriter(stream, fieldnames=fields)
                    writer.writeheader()
                    for i in range(2):
                        writer.writerow(dict(session=session, inventory=10, slot=i, pide=start+i, item=100, fingerprint="identical"))
            report = compare(left, right)
            self.assertEqual(report["duplicate_fingerprints_before"], 1)
            self.assertEqual(report["shared_fingerprint_groups"], 1)
            self.assertFalse(report["persistent_identity_verified"])

    def test_corrupt_capture_is_refused(self):
        with tempfile.TemporaryDirectory(prefix="bootlab-compare-") as folder:
            file = Path(folder) / "broken.csv"
            file.write_text("session,inventory,slot,pide,item,fingerprint\ninvalid,10,0,20,100,x\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                compare(file, file)


if __name__ == "__main__":
    unittest.main()
