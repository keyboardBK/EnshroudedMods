import unittest
from audit_schema import audit, KNOWN, OFFSETS


class AuditTests(unittest.TestCase):
    def fixture(self):
        return {"version": "test", "types": [
            {"qualifiedName": "keen::ecs::" + name, "size": size,
             "structFields": {field: {"dataOffset": OFFSETS[name][field]} for field in fields}}
            for name, (size, fields) in KNOWN.items()]}

    def test_known_fields_never_enable_writes(self):
        data = self.fixture()
        report = audit(data)
        self.assertTrue(all(t["status"] == "known" for t in report["types"]))
        self.assertIsNone(report["verified_custom_saved_field"])
        self.assertFalse(report["writes_enabled"])

    def test_new_field_or_layout_requires_investigation(self):
        data = self.fixture()
        data["types"][0]["structFields"]["padding_or_custom"] = {"dataOffset": 18}
        report = audit(data)
        self.assertEqual(report["types"][0]["status"], "changed")
        self.assertFalse(report["writes_enabled"])

    def test_missing_type_is_reported(self):
        self.assertTrue(all(t["status"] == "missing" for t in audit({"types": []})["types"]))

    def test_offset_change_requires_investigation(self):
        data = self.fixture()
        data["types"][0]["structFields"]["itemId"]["dataOffset"] = 4
        self.assertEqual(audit(data)["types"][0]["status"], "changed")


if __name__ == "__main__":
    unittest.main()
