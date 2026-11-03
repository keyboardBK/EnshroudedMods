"""Read-only validation of actual kfc-parser JSON exports and EML variants.csv.
Usage: python validate_assets.py ORIGINAL_DIR PATCHED_DIR VARIANTS_CSV
ORIGINAL_DIR must contain ItemInfo and ItemRegistryResource from unmodified data.
PATCHED_DIR must additionally contain UiTextureResource. No game data is shipped here.
"""
import csv
import json
from pathlib import Path
import sys


def resources(root, kind):
    return {p.name: json.loads(p.read_text(encoding="utf-8-sig"))
            for p in (root / kind).glob("*.json")}


def validate(original_dir, patched_dir, variants_csv):
    original = resources(original_dir, "ItemInfo")
    loaded = resources(patched_dir, "ItemInfo")
    assert original, "No original ItemInfo exports"
    for name, data in original.items():
        assert loaded[name] == data, f"Original item changed: {name}"
    variants = {d["itemId"]["value"]: d for d in loaded.values()
                if d["debugName"].startswith("SM02_Boot_")}
    by_base = {d["itemId"]["value"]: d for d in original.values()}
    icons = {name.split("_")[0]: d for name, d in resources(patched_dir, "UiTextureResource").items()}
    with variants_csv.open() as f:
        rows = list(csv.DictReader(f))
    assert rows and len(rows) == len(variants)
    old_registry = next(iter(resources(original_dir, "ItemRegistryResource").values()))
    registry = next(iter(resources(patched_dir, "ItemRegistryResource").values()))
    assert len(registry["itemRefs"]) == len(registry["dbgNames"])
    assert len(set(registry["itemRefs"])) == len(registry["itemRefs"])
    assert len(registry["itemRefs"]) == len(old_registry["itemRefs"]) + len(variants)
    registered = dict(zip(registry["itemRefs"], registry["dbgNames"]))

    def sprint(d):
        return sum(c["$value"]["value"] for c in d["impactValues"]["simple"]
                   if c["$value"]["configId"]["value"] == 345127019)

    def ui(d):
        return sum(c["value"] for c in d["uiValues"] if c["locaId"]["value"] == 3180863901)

    changed_fields = {"itemId", "objectId", "debugName", "impactValues", "uiValues", "$guid"}
    counts = {}
    for row in rows:
        base_id = int(row["base_id"], 16)
        base = by_base[base_id]
        v = variants[int(row["variant_id"], 16)]
        bonus = int(row["bonus_percent"])
        assert v["debugName"] == f"SM02_Boot_{base_id:08x}_P{bonus:03d}"
        assert v["$guid"] == row["variant_guid"]
        assert v["objectId"] == base["objectId"], "Native UI must reuse the original icon GUID"
        assert row["variant_guid"] not in icons, "Duplicate variant icon wastes the native UI table"
        assert v["objectId"] in icons
        assert registered[row["variant_guid"]] == v["debugName"]
        assert base["objectId"] in registered, "Unregistered legacy source boot"
        assert abs(sprint(v) - sprint(base) - 8 * bonus / 100) < 0.00001
        assert ui(v) == ui(base) + bonus
        def other_configs(d):
            return [c for c in d["impactValues"]["simple"]
                    if c["$value"]["configId"]["value"] != 345127019]
        assert other_configs(v) == other_configs(base)
        assert v["impactValues"]["scaled"] == base["impactValues"]["scaled"]
        assert [u for u in v["uiValues"] if u["locaId"]["value"] != 3180863901] == [
            u for u in base["uiValues"] if u["locaId"]["value"] != 3180863901]
        assert {k: x for k, x in v.items() if k not in changed_fields} == {
            k: x for k, x in base.items() if k not in changed_fields}
        counts.setdefault(base_id, set()).add(bonus)
    assert all(bonuses == set(range(101)) for bonuses in counts.values())
    expected_boots = {i for i, d in by_base.items() if d["objectId"] in registered
                      and d["equipment"]["slot"] == "Armour_Feet" and "IsVanityItem" not in d["flags"]}
    assert set(counts) == expected_boots, "A registered gameplay boot was omitted"
    return {"version": "0.2.1-alpha", "build": 1283531,
            "original_item_definitions_unchanged": len(original),
            "registered_boots": len(counts), "persistent_variants_verified": len(rows),
            "icons_registry_and_native_sprint_tooltip_verified": True,
            "live_world_save_multiplayer_tested": False}


if __name__ == "__main__":
    print(json.dumps(validate(*(Path(p) for p in sys.argv[1:])), indent=2))
