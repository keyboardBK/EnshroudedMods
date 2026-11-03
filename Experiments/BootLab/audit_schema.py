"""Read-only audit of the locally extracted game reflection; never edits a save."""
import argparse
import json
from pathlib import Path

KNOWN = {
    "ItemState": (20, {
        "containedInSlotId": "current owner/slot; changes when moved",
        "itemId": "item definition identity; existing variant approach",
        "baseDamageUi": "game UI cache; not custom persistent metadata",
        "itemState": "game equipment/state flags",
        "itemRarityUi": "game rarity UI cache",
        "itemLevelUi": "game level UI cache",
    }),
    "PerkContainerNew": (2, {
        "availablePerks": "native number of available perks; determines rarity",
        "unlockedPerks": "native upgrade progression",
    }),
    "PerkContainer": (28, {
        "perkIds": "native perk identities, not an arbitrary bonus scalar",
        "perkIsUnlocked": "native perk unlock bits",
    }),
    "Durability": (16, {
        "dataStorage": "native durability values",
    }),
    "ColorPaletteItem": (4, {"paletteItemId": "native dye identity"}),
    "WeaponGemSlot": (12, {"weaponGem": "native socketed gem stack"}),
    "UsedItem": (4, {"itemId": "native item identity"}),
}

OFFSETS = {
    "ItemState": {"containedInSlotId": 0, "itemId": 8, "baseDamageUi": 12, "itemState": 14, "itemRarityUi": 16, "itemLevelUi": 17},
    "PerkContainerNew": {"availablePerks": 0, "unlockedPerks": 1},
    "PerkContainer": {"perkIds": 0, "perkIsUnlocked": 20},
    "Durability": {"dataStorage": 8},
    "ColorPaletteItem": {"paletteItemId": 0},
    "WeaponGemSlot": {"weaponGem": 0},
    "UsedItem": {"itemId": 0},
}


def audit(data):
    types = {t["qualifiedName"]: t for t in data["types"]}
    result = []
    for name, (size, fields) in KNOWN.items():
        qualified = "keen::ecs::" + name
        entry = types.get(qualified)
        if entry is None:
            result.append({"type": qualified, "status": "missing"})
            continue
        actual = entry.get("structFields", {})
        result.append({
            "type": qualified, "size": entry["size"],
            "status": "known" if entry["size"] == size and set(actual) == set(fields) and
                all(info["dataOffset"] == OFFSETS[name].get(field) for field, info in actual.items()) else "changed",
            "fields": [{"name": f, "offset": info["dataOffset"],
                        "role": fields.get(f, "unknown: investigate; do not write")}
                       for f, info in actual.items()],
        })
    return {
        "game_version": data.get("version"),
        "types": result,
        "verified_custom_saved_field": None,
        "writes_enabled": False,
        "note": "Reflection layout alone does not prove serialization or network replication. Padding is not a declared field. No listed native field is treated as spare storage."
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("types_json", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.dumps(audit(json.loads(args.types_json.read_text(encoding="utf-8-sig"))),
                        ensure_ascii=False, indent=2) + "\n"
    if args.output:
        if args.output.resolve() == args.types_json.resolve():
            parser.error("Output must differ from the input")
        args.output.write_text(report, encoding="utf-8")
    else:
        print(report, end="")
